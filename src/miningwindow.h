#pragma once
#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include "miner.h"

class HashrateGraph;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;

// The mining cockpit: a live RandomX mining dashboard driven by MinerEngine.
// One window per browser process. It mines the browser's own identity account.
//
// Server-target state machine (the target is the server whose main port gets
// the PoW tickets):
//   Idle     - the target field follows the last-browsed server (editable);
//              Start is armed.
//   Starting - connecting; the target is locked.
//   Mining   - hashing; the target is sticky (browsing no longer changes it).
//   Stopping - winding down; on exit -> Idle, which re-inherits the last
//              browsed server.
class MiningWindow : public QWidget {
  Q_OBJECT
public:
  explicit MiningWindow(cwb::MinerEngine* engine, QWidget* parent = nullptr);

signals:
  // The cockpit is only a view onto the application-owned mining task.
  // Closing the view releases the browser's latched cockpit button.
  void cockpitClosed();

public:

  // Called by the main window whenever the user browses a CES server. Applied
  // to the target field only while Idle; ignored (but remembered) while mining.
  void setInheritedServer(const QString& server);

  // Test/harness entry: set the target and press Start, no user click.
  void startFromCli(const QString& server);

  // The auto-miner (the credit band): start/stop initiated by the browser's
  // funding heuristic, not the user. autoStart only fires while Idle and not
  // manually suspended; autoStop only ends sessions the heuristic started (a
  // manual session never auto-stops). A manual Stop suspends the heuristic
  // for this app session; the checkbox re-arms it.
  void autoStart(const QString& server);
  void autoStop();
  bool idle() const { return state_ == State::Idle; }
  bool autoSession() const { return autoSession_; }
  bool autoSuspended() const { return autoSuspended_; }

  // The browser's periodic balance poll pushes here, so the Balance card shows
  // your money even when idle (not just what the engine sampled while mining).
  void setExternalBalance(qint64 sumUnits);

 signals:
  // The engine sampled a fresh account balance (per solution): the browser
  // syncs its status-bar figure to this so the two never diverge.
  void balanceChanged(qint64 units);

 protected:
  void closeEvent(QCloseEvent*) override;

 private:
  enum class State { Idle, Starting, Mining, Stopping };
  void setState(State s);
  void onStartStop();
  void refreshReadout();        // recompute + repaint the derived figures
  void refreshSettingsCards();  // repaint the active threads/difficulty/throttle

  cwb::MinerEngine* engine_ = nullptr;  // application service; the window is only its UI
  State state_ = State::Idle;
  QString inheritedServer_ = QStringLiteral("ces.pubcom.org");

  // Live inputs, updated by engine signals; read by refreshReadout().
  int serverMinDiff_ = 0;
  quint64 totalHashes_ = 0;
  double windowHashrate_ = 0;
  quint64 solutions_ = 0;
  quint64 creditUnits_ = 0;
  qint64 balanceUnits_ = 0;
  bool balanceOk_ = false;
  qint64 externalBalance_ = -1;  // total pushed by the browser poll; <0 unknown
  QElapsedTimer elapsed_;

  // The knob values actually in effect (echoed by the engine while mining, or
  // mirrored from the spinboxes while idle). The readout uses these, not the
  // spinboxes, so the top cards show what is really running.
  int activeThreads_ = 1;
  int activeExtraDiff_ = 0;
  int activeThrottle_ = 0;

  // Cumulative energy the session has spent, for the USD-equivalent readout.
  int hwThreads_ = 1;
  double energyKwh_ = 0;
  qint64 lastEnergyMs_ = 0;

  // Chrome.
  QLineEdit* target_ = nullptr;
  QPushButton* startStop_ = nullptr;
  QLabel* pill_ = nullptr;
  QLabel* elapsedHead_ = nullptr;  // session time, shown next to the pill
  QLabel* statusLine_ = nullptr;
  HashrateGraph* graph_ = nullptr;
  QToolButton* advToggle_ = nullptr;
  QWidget* advPanel_ = nullptr;
  QSlider* threads_ = nullptr;
  QSlider* extraDiff_ = nullptr;
  QSlider* throttle_ = nullptr;
  QLabel* threadsVal_ = nullptr;
  QLabel* diffVal_ = nullptr;
  QLabel* throttleVal_ = nullptr;
  // Auto-mining band controls (Advanced): factory-on, min 10 / max 20 whole
  // credits, sliders 0..1000 with printed values.
  class QCheckBox* autoChk_ = nullptr;
  QSlider* autoMin_ = nullptr;
  QSlider* autoMax_ = nullptr;
  QLabel* autoMinVal_ = nullptr;
  QLabel* autoMaxVal_ = nullptr;
  bool autoSession_ = false;    // this mining session was heuristic-started
  bool autoSuspended_ = false;  // manual Stop parks the heuristic this session
  QTimer* uiTimer_ = nullptr;

  // Metric value/sub labels.
  QLabel* vHashrate_ = nullptr;
  QLabel* sHashrate_ = nullptr;
  QLabel* vBalance_ = nullptr;
  QLabel* vUsdeee_ = nullptr;
  QLabel* sUsdeee_ = nullptr;
  QLabel* vEarned_ = nullptr;
  QLabel* sEarned_ = nullptr;
  QLabel* vSolutions_ = nullptr;
  QLabel* sSolutions_ = nullptr;
  QLabel* vPerDay_ = nullptr;
  QLabel* sPerHour_ = nullptr;
  QLabel* vObserved_ = nullptr;
  QLabel* vEta_ = nullptr;
  // Active-settings cards (cyan): what threads / difficulty / throttle the
  // miner is using right now.
  QLabel* vThreadsC_ = nullptr;
  QLabel* sThreadsC_ = nullptr;
  QLabel* vDifficulty_ = nullptr;
  QLabel* sDifficulty_ = nullptr;
  QLabel* vThrottleC_ = nullptr;
};
