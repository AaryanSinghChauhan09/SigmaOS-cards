#pragma once

#include "klondike.h"
#include <QLockFile>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

// The game the window shows: the deal in play, what is picked up, the clock,
// and the preferences, all saved after every move.
class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList cards READ cards NOTIFY changed)
  Q_PROPERTY(QVariantList piles READ piles NOTIFY changed)
  Q_PROPERTY(QVariantList targets READ targets NOTIFY changed)
  Q_PROPERTY(int selectedPile READ selectedPile NOTIFY changed)
  Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
  Q_PROPERTY(int hintFrom READ hintFrom NOTIFY changed)
  Q_PROPERTY(int hintIndex READ hintIndex NOTIFY changed)
  Q_PROPERTY(int hintTo READ hintTo NOTIFY changed)
  Q_PROPERTY(bool stuck READ stuck NOTIFY changed)
  Q_PROPERTY(int moves READ moves NOTIFY changed)
  Q_PROPERTY(int home READ home NOTIFY changed)
  Q_PROPERTY(int stock READ stock NOTIFY changed)
  Q_PROPERTY(bool won READ won NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY preferencesChanged)
  Q_PROPERTY(int series READ series WRITE setSeries NOTIFY preferencesChanged)
  Q_PROPERTY(bool saveHealthy READ saveHealthy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit Backend(const QString &savePath = {}, QObject *parent = nullptr);
  ~Backend() override;
  QVariantList cards() const { return m_cards; }
  QVariantList piles() const { return m_pileInfo; }
  QVariantList targets() const;
  int selectedPile() const { return m_selectedPile; }
  int selectedIndex() const { return m_selectedIndex; }
  int hintFrom() const { return m_hintFrom; }
  int hintIndex() const { return m_hintIndex; }
  int hintTo() const { return m_hintTo; }
  bool stuck() const { return m_stuck; }
  int moves() const { return int(m_game.moves().size()); }
  int home() const { return m_game.home(); }
  int stock() const { return int(m_game.pile(Klondike::Stock).size()); }
  bool won() const { return m_game.won(); }
  bool canUndo() const { return !m_game.moves().empty(); }
  int elapsed() const { return m_elapsed; }
  bool active() const { return m_active; }
  int series() const { return m_series; }
  bool saveHealthy() const { return ownsSave() && !m_saveBlocked && !m_saveFailed; }
  QString error() const { return m_error; }
  bool ownsSave() const { return m_savePath == ":memory:" || (m_saveLock && m_saveLock->isLocked()); }
  const Klondike::Game &game() const { return m_game; }

  // A click: picks up the cards from `index` in `pile`, puts down what is
  // held when they may go there, and turns the stock.
  Q_INVOKABLE void activate(int pile, int index);
  // A double click: sends the cards to the best place they can go.
  Q_INVOKABLE bool autoPlay(int pile, int index);
  // A drag let go over `to`.
  Q_INVOKABLE bool drop(int pile, int index, int to);
  Q_INVOKABLE void draw();
  Q_INVOKABLE void cancelSelection();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void hint();
  // Deals at once. A seed of 0 deals at random; the tests name theirs.
  Q_INVOKABLE void newGame(quint32 seed = 0);
  Q_INVOKABLE void dismissError();
  void setActive(bool value);
  void setSeries(int value);
signals:
  void changed();
  void elapsedChanged();
  void preferencesChanged();
private:
  bool move(int from, int to, int count);
  void played();
  void clearMarks();
  void refresh();
  void finishStep();
  void save();
  void restore();
  Klondike::Game m_game;
  std::unique_ptr<QLockFile> m_saveLock;
  QVariantList m_cards, m_pileInfo;
  int m_selectedPile = -1, m_selectedIndex = -1;
  int m_hintFrom = -1, m_hintIndex = -1, m_hintTo = -1;
  bool m_stuck = false;
  QString m_error, m_savePath;
  int m_elapsed = 0, m_series = 0;
  bool m_active = true, m_saveBlocked = false, m_saveFailed = false;
  QTimer m_clock, m_finish;
};
