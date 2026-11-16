#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

using namespace Klondike;

namespace {
quint32 randomSeed() { return QRandomGenerator::global()->bounded(1u, 0xffffffffu); }
}

Backend::Backend(const QString &savePath, QObject *parent) : QObject(parent), m_game(randomSeed()), m_savePath(savePath) {
  if (m_savePath.isEmpty()) m_savePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/game.json";
  if (m_savePath != ":memory:") {
    QDir().mkpath(QFileInfo(m_savePath).absolutePath());
    m_saveLock = std::make_unique<QLockFile>(m_savePath + ".lock");
    m_saveLock->setStaleLockTime(0);
    if (!m_saveLock->tryLock(0)) {
      m_saveBlocked = true;
      m_error = tr("Another Cards window owns this saved game, or its folder is not writable.");
    }
  }
  // The clock is the game's own time, not a poll: it runs only while a game
  // is under way and its window is the one in front.
  m_clock.setInterval(1000);
  connect(&m_clock, &QTimer::timeout, this, [this] { ++m_elapsed; emit elapsedChanged(); });
  m_finish.setInterval(70);
  connect(&m_finish, &QTimer::timeout, this, &Backend::finishStep);
  if (ownsSave()) restore();
  refresh();
}

Backend::~Backend() {
  // The seconds since the last move are worth keeping too.
  if (canUndo()) save();
}

QVariantList Backend::targets() const {
  QVariantList result;
  if (m_selectedPile >= 0)
    for (int pile : m_game.targets(m_selectedPile, m_selectedIndex)) result.append(pile);
  return result;
}

void Backend::refresh() {
  if (m_cards.isEmpty())
    for (int id = 0; id < 52; ++id) m_cards.append(QVariantMap());
  m_pileInfo.clear();
  for (int pile = 0; pile < Piles; ++pile) {
    const auto &cards = m_game.pile(pile);
    const int size = int(cards.size());
    // The waste shows its last three cards side by side.
    const int fanned = pile == Waste ? std::min(size, int(Draw)) : 0;
    int down = 0, up = 0;
    for (int index = 0; index < size; ++index) {
      m_cards[cards[index].id] = QVariantMap{{"pile", pile}, {"index", index}, {"up", cards[index].up}, {"down", down},
                                             {"upBelow", up}, {"fan", std::max(0, index - (size - fanned))}};
      cards[index].up ? ++up : ++down;
    }
    m_pileInfo.append(QVariantMap{{"count", size}, {"down", down}, {"up", up}});
  }
  const bool running = m_active && canUndo() && !won();
  if (running != m_clock.isActive()) running ? m_clock.start() : m_clock.stop();
  emit changed();
}

void Backend::clearMarks() {
  m_selectedPile = m_selectedIndex = -1;
  m_hintFrom = m_hintIndex = m_hintTo = -1;
  m_stuck = false;
}

void Backend::played() {
  clearMarks();
  refresh();
  save();
  if (!m_game.finishable()) return;
  m_finish.start();
}

void Backend::finishStep() {
  if (m_game.finishStep()) refresh();
  if (!m_game.finishable()) {
    m_finish.stop();
    save();
  }
}

bool Backend::move(int from, int to, int count) {
  if (won() || !m_game.play(from, to, count)) return false;
  played();
  return true;
}

void Backend::activate(int pile, int index) {
  if (won() || m_finish.isActive() || pile < 0 || pile >= Piles) return;
  if (pile == Stock) { draw(); return; }
  const auto &cards = m_game.pile(pile);
  if (m_selectedPile >= 0 && pile != m_selectedPile && m_game.canMove(m_selectedPile, m_selectedIndex, pile)) {
    move(m_selectedPile, pile, int(m_game.pile(m_selectedPile).size()) - m_selectedIndex);
    return;
  }
  // Only the top card of the waste or a foundation can be picked up.
  if (pile < Tableau) index = int(cards.size()) - 1;
  const bool pickable = index >= 0 && index < int(cards.size()) && cards[index].up;
  const bool same = pile == m_selectedPile && index == m_selectedIndex;
  clearMarks();
  if (pickable && !same) { m_selectedPile = pile; m_selectedIndex = index; }
  emit changed();
}

bool Backend::autoPlay(int pile, int index) {
  if (won() || m_finish.isActive() || pile < 0 || pile >= Piles) return false;
  if (pile == Stock) { draw(); return true; }
  const int size = int(m_game.pile(pile).size());
  if (pile < Tableau) index = size - 1;
  const int to = m_game.bestTarget(pile, index);
  return to >= 0 && move(pile, to, size - index);
}

bool Backend::drop(int pile, int index, int to) {
  if (won() || m_finish.isActive() || pile < 0 || pile >= Piles || !m_game.canMove(pile, index, to)) return false;
  return move(pile, to, int(m_game.pile(pile).size()) - index);
}

void Backend::draw() {
  if (won() || m_finish.isActive()) return;
  if (m_game.drawStock()) played();
}

void Backend::cancelSelection() {
  clearMarks();
  emit changed();
}

void Backend::undo() {
  m_finish.stop();
  if (!m_game.undo()) return;
  clearMarks();
  refresh();
  save();
}

void Backend::hint() {
  if (won() || m_finish.isActive()) return;
  clearMarks();
  if (const auto move = m_game.hint()) {
    m_hintFrom = move->from == Waste && move->to == Stock ? int(Stock) : move->from;
    m_hintIndex = move->to == Stock ? -1 : int(m_game.pile(move->from).size()) - move->count;
    if (move->from == Stock) m_hintIndex = int(m_game.pile(Stock).size()) - 1;
    m_hintTo = move->from == Stock || move->to == Stock ? -1 : move->to;
  } else {
    m_stuck = true;
  }
  emit changed();
}

void Backend::newGame(quint32 seed) {
  m_finish.stop();
  if (!ownsSave()) return;
  m_saveBlocked = false;
  m_error.clear();
  m_game = Game(seed ? seed : randomSeed());
  m_elapsed = 0;
  emit elapsedChanged();
  clearMarks();
  refresh();
  save();
}

void Backend::dismissError() { m_error.clear(); emit changed(); }

void Backend::setActive(bool value) {
  if (value == m_active) return;
  m_active = value;
  emit preferencesChanged();
  refresh();
}
void Backend::setSeries(int value) {
  if (value < 0 || value > 3 || value == m_series) return;
  m_series = value; emit preferencesChanged(); save();
}

void Backend::save() {
  if (m_savePath == ":memory:" || m_saveBlocked || !ownsSave()) return;
  QJsonArray moves;
  for (const Move &move : m_game.moves()) moves.append(QJsonArray{move.from, move.to, move.count});
  QJsonObject data{{"version", 1}, {"seed", qint64(m_game.seed())}, {"moves", moves}, {"elapsed", m_elapsed},
                   {"series", m_series}};
  QDir().mkpath(QFileInfo(m_savePath).absolutePath());
  QSaveFile file(m_savePath);
  file.setDirectWriteFallback(false);
  const auto bytes = QJsonDocument(data).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    m_saveFailed = true;
    m_error = tr("Could not save your game: %1").arg(file.errorString()); emit changed();
  } else if (m_saveFailed) {
    m_saveFailed = false; m_error.clear(); emit changed();
  }
}

void Backend::restore() {
  if (m_savePath == ":memory:" || !QFile::exists(m_savePath)) return;
  QFile file(m_savePath);
  if (!file.open(QIODevice::ReadOnly)) { m_saveBlocked = true; m_error = tr("Could not open your saved game: %1").arg(file.errorString()); return; }
  if (file.size() > 2 * 1024 * 1024) {
    m_saveBlocked = true; m_error = tr("The saved game is too large to open."); return;
  }
  QJsonParseError error;
  const auto data = QJsonDocument::fromJson(file.readAll(), &error).object();
  const qint64 seed = data["seed"].toInteger();
  // Saves from when one card could be drawn at a time are not this game.
  const int draw = data["draw"].toInt(Draw);
  bool valid = error.error == QJsonParseError::NoError && data["version"].toInt() == 1 && data["moves"].isArray()
               && data["moves"].toArray().size() <= 100000 && seed > 0 && seed <= 0xffffffffll && draw == Draw;
  Game game{quint32(seed)};
  if (valid) {
    for (const auto value : data["moves"].toArray()) {
      const auto entry = value.toArray();
      if (entry.size() != 3 || game.won() || !game.play(entry[0].toInt(-1), entry[1].toInt(-1), entry[2].toInt(-1))) { valid = false; break; }
    }
  }
  if (!valid) {
    // Keep the damaged save intact until the player explicitly starts again.
    m_saveBlocked = true;
    m_error = tr("The saved game is damaged. Start a new game to replace it.");
    return;
  }
  m_game = game;
  m_elapsed = std::clamp(data["elapsed"].toInt(), 0, 359999);
  m_series = std::clamp(data["series"].toInt(), 0, 3);
}
