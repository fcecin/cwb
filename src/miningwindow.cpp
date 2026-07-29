#include "miningwindow.h"
#include "settings.h"

#include "cesidentity.h"
#include "hashrategraph.h"
#include "minermath.h"

#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <thread>

namespace {

QString fmtHashrate(double hps) {
  if (hps <= 0) return QStringLiteral("0 H/s");
  if (hps >= 1e6) return QString::number(hps / 1e6, 'f', 2) + " MH/s";
  if (hps >= 1e3) return QString::number(hps / 1e3, 'f', 2) + " kH/s";
  return QString::number(hps, 'f', 0) + " H/s";
}

QString fmtCreditsFull(double credits) {
  return QString::number(credits, 'f', 8);
}

QString fmtCreditsRate(double credits) {
  return QString::number(credits, 'f', credits >= 1 ? 3 : 6);
}

QString fmtDuration(double s) {
  if (s <= 0) return QStringLiteral("--");
  if (s < 1) return QString::number(s * 1000, 'f', 0) + " ms";
  if (s < 60) return QString::number(s, 'f', 1) + " s";
  if (s < 3600)
    return QString::number(int(s) / 60) + "m " + QString::number(int(s) % 60) + "s";
  if (s < 86400)
    return QString::number(int(s) / 3600) + "h " +
           QString::number((int(s) % 3600) / 60) + "m";
  return QString::number(s / 86400.0, 'f', 1) + " days";
}

const char* kStyle = R"CSS(
QWidget#miner { background:#0b0e12; color:#e6e9ef; font-family:sans-serif; }
QLabel { color:#d7dce5; }
QLabel#title { font-size:20px; font-weight:bold; color:#eef2f7; }
QLabel#pill { font-size:11px; font-weight:bold; padding:3px 10px; border-radius:9px; }
QLabel#hint { color:#6f7b8c; font-size:11px; }
QLineEdit { background:#141a22; border:1px solid #2a3340; border-radius:6px;
            padding:6px 9px; color:#e6e9ef; selection-background-color:#2a3646; }
QLineEdit:disabled { color:#6f7b8c; background:#0f141a; }
QFrame#card { background:#131920; border:1px solid #212a35; border-radius:10px; }
QFrame#cardProj { background:#15130d; border:1px solid #33301c; border-radius:10px; }
QFrame#cardSet { background:#0c1417; border:1px solid #1b3138; border-radius:10px; }
QFrame#cardMoney { background:#0c150f; border:1px solid #1c3324; border-radius:10px; }
QLabel#cardTitle { color:#7b8798; font-size:10px; font-weight:bold; letter-spacing:1px; }
QLabel#cardValue { color:#eef2f7; font-size:22px; font-weight:bold; }
QLabel#cardValueProj { color:#dcb46a; font-size:22px; font-weight:bold; }
QLabel#cardValueSet { color:#57c6db; font-size:22px; font-weight:bold; }
QLabel#cardValueMoney { color:#5fce87; font-size:22px; font-weight:bold; }
QLabel#elapsedHead { color:#8b93a3; font-size:12px; }
QLabel#cardSub { color:#6f7b8c; font-size:11px; }
QLabel#status { color:#8b93a3; font-size:12px; }
QLabel#advLabel { color:#aeb6c2; font-size:12px; }
QCheckBox { color:#d7dce5; spacing:7px; }
QCheckBox:disabled { color:#707987; }
QToolButton { color:#c9cedb; border:1px solid #2a3340; border-radius:6px;
              padding:5px 12px; font-size:12px; background:#141a22; }
QToolButton:hover { color:#eef2f7; background:#1a212b; border-color:#3a4657; }
QToolButton:checked { color:#57c6db; border-color:#274c56; }
QLabel#knobVal { color:#57c6db; font-size:13px; font-weight:bold; min-width:64px; }
QSlider::groove:horizontal { height:5px; background:#232c39; border-radius:3px; }
QSlider::sub-page:horizontal { background:#2f8fa3; border-radius:3px; }
QSlider::handle:horizontal { width:15px; height:15px; margin:-6px 0;
           border-radius:8px; background:#7fd6e6; border:1px solid #0b0e12; }
QSlider::handle:horizontal:hover { background:#a3e4f0; }
QPushButton#go { background:#1f8a4c; color:#fff; border:none; border-radius:8px;
                 padding:10px 26px; font-size:14px; font-weight:bold; }
QPushButton#go:hover { background:#25a15a; }
QPushButton#stop { background:#a13636; color:#fff; border:none; border-radius:8px;
                   padding:10px 26px; font-size:14px; font-weight:bold; }
QPushButton#stop:disabled { background:#3a2a2a; color:#8a7a7a; }
QWidget#adv { background:#0f141a; border:1px solid #1c2530; border-radius:8px; }
)CSS";

struct Card {
  QLabel* value;
  QLabel* sub;
};

enum CardKind { Measured, Projected, Setting, Money };

Card makeCard(QGridLayout* grid, int r, int c, const QString& title,
              CardKind kind) {
  auto* frame = new QFrame;
  frame->setObjectName(kind == Projected ? "cardProj"
                       : kind == Setting  ? "cardSet"
                       : kind == Money    ? "cardMoney"
                                          : "card");
  auto* v = new QVBoxLayout(frame);
  v->setContentsMargins(13, 10, 13, 10);
  v->setSpacing(2);
  auto* t = new QLabel(title.toUpper());
  t->setObjectName("cardTitle");
  auto* val = new QLabel("--");
  val->setObjectName(kind == Projected ? "cardValueProj"
                     : kind == Setting  ? "cardValueSet"
                     : kind == Money    ? "cardValueMoney"
                                        : "cardValue");
  auto* sub = new QLabel(QString());
  sub->setObjectName("cardSub");
  v->addWidget(t);
  v->addWidget(val);
  v->addWidget(sub);
  grid->addWidget(frame, r, c);
  return {val, sub};
}

}  // namespace

MiningWindow::MiningWindow(cwb::MinerEngine* engine, QWidget* parent)
    : QWidget(parent), engine_(engine) {
  Q_ASSERT(engine_);
  setObjectName("miner");
  setWindowTitle(tr("cwb miner"));
  setStyleSheet(kStyle);
  resize(720, 620);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 16, 18, 14);
  root->setSpacing(12);

  auto* head = new QHBoxLayout;
  auto* title = new QLabel(tr("Miner"));
  title->setObjectName("title");
  elapsedHead_ = new QLabel;
  elapsedHead_->setObjectName("elapsedHead");
  pill_ = new QLabel;
  pill_->setObjectName("pill");
  head->addWidget(title);
  head->addStretch();
  head->addWidget(elapsedHead_);
  head->addSpacing(10);
  head->addWidget(pill_);
  root->addLayout(head);

  auto* tgt = new QHBoxLayout;
  auto* tl = new QLabel(tr("Server"));
  target_ = new QLineEdit;
  target_->setPlaceholderText(QStringLiteral("ces.pubcom.org"));
  auto* hint = new QLabel(tr("PoW tickets ride the main port (default 53830)"));
  hint->setObjectName("hint");
  tgt->addWidget(tl);
  tgt->addWidget(target_, 1);
  tgt->addWidget(hint);
  root->addLayout(tgt);

  graph_ = new HashrateGraph;
  root->addWidget(graph_);

  auto* grid = new QGridLayout;
  grid->setSpacing(9);
  Card hr = makeCard(grid, 0, 0, tr("Hashrate"), Measured);
  Card bal = makeCard(grid, 0, 1, tr("Balance"), Measured);
  Card usdeee = makeCard(grid, 0, 2, tr("USDEEE"), Money);
  Card sol = makeCard(grid, 0, 3, tr("Solutions"), Measured);
  Card perday = makeCard(grid, 1, 0, tr("Projected floor"), Projected);
  Card observed = makeCard(grid, 1, 1, tr("Observed"), Measured);
  Card eta = makeCard(grid, 1, 2, tr("Next solution ~"), Projected);
  Card earned = makeCard(grid, 1, 3, tr("Earned this session"), Measured);
  root->addLayout(grid);
  vHashrate_ = hr.value;      sHashrate_ = hr.sub;
  vBalance_ = bal.value;      bal.sub->setText(tr("your balance"));
  vUsdeee_ = usdeee.value;    sUsdeee_ = usdeee.sub;
  vSolutions_ = sol.value;    sSolutions_ = sol.sub;
  vPerDay_ = perday.value;    sPerHour_ = perday.sub;
  vObserved_ = observed.value; observed.sub->setText(tr("from earnings"));
  vEta_ = eta.value;
  vEarned_ = earned.value;    sEarned_ = earned.sub;

  // Active-settings strip (cyan): what the miner is using right now. Set the
  // values below in Advanced; they reflect up here when the worker adopts them.
  auto* sgrid = new QGridLayout;
  sgrid->setSpacing(9);
  Card cThreads = makeCard(sgrid, 0, 0, tr("Threads in use"), Setting);
  Card cDiff = makeCard(sgrid, 0, 1, tr("Difficulty"), Setting);
  Card cThrottle = makeCard(sgrid, 0, 2, tr("Throttle"), Setting);
  root->addLayout(sgrid);
  vThreadsC_ = cThreads.value;  sThreadsC_ = cThreads.sub;
  vDifficulty_ = cDiff.value;   sDifficulty_ = cDiff.sub;
  vThrottleC_ = cThrottle.value;

  advToggle_ = new QToolButton;
  advToggle_->setText(tr("Advanced  -  set threads, difficulty, throttle"));
  advToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advToggle_->setArrowType(Qt::RightArrow);
  advToggle_->setCheckable(true);
  root->addWidget(advToggle_, 0, Qt::AlignLeft);

  advPanel_ = new QWidget;
  advPanel_->setObjectName("adv");
  auto* adv = new QGridLayout(advPanel_);
  adv->setContentsMargins(16, 12, 16, 12);
  adv->setHorizontalSpacing(14);
  adv->setVerticalSpacing(10);
  const int hw = std::max(1, int(std::thread::hardware_concurrency()));
  hwThreads_ = hw;
  auto mkSlider = [](int lo, int hi, int val) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setValue(val);
    s->setMinimumWidth(200);
    return s;
  };
  threads_ = mkSlider(1, hw, std::max(1, hw / 2));
  extraDiff_ = mkSlider(0, 16, 0);
  throttle_ = mkSlider(0, 2000, 0);  // us of sleep between batches
  threadsVal_ = new QLabel;
  diffVal_ = new QLabel;
  throttleVal_ = new QLabel;
  for (QLabel* l : {threadsVal_, diffVal_, throttleVal_})
    l->setObjectName("knobVal");
  const char* names[3] = {"Threads", "Extra difficulty", "Throttle"};
  QSlider* sliders[3] = {threads_, extraDiff_, throttle_};
  QLabel* vals[3] = {threadsVal_, diffVal_, throttleVal_};
  for (int i = 0; i < 3; ++i) {
    auto* l = new QLabel(tr(names[i]));
    l->setObjectName("advLabel");
    adv->addWidget(l, i, 0);
    adv->addWidget(sliders[i], i, 1);
    adv->addWidget(vals[i], i, 2);
  }
  adv->setColumnStretch(1, 1);
  advPanel_->setVisible(false);
  root->addWidget(advPanel_);
  connect(advToggle_, &QToolButton::toggled, this, [this](bool on) {
    advPanel_->setVisible(on);
    advToggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
  });

  // The knobs are always editable. A change goes live immediately while mining
  // (the engine adopts it next batch and echoes it to the cards); while idle it
  // just updates the preview cards and seeds the next run. The little labels by
  // each slider track the slider itself as you drag.
  activeThreads_ = threads_->value();
  auto syncKnobLabels = [this]() {
    threadsVal_->setText(QString::number(threads_->value()));
    diffVal_->setText(QStringLiteral("+") + QString::number(extraDiff_->value()));
    throttleVal_->setText(throttle_->value() > 0
                              ? QString::number(throttle_->value()) + tr(" us")
                              : tr("off"));
  };
  auto onKnob = [this, syncKnobLabels]() {
    syncKnobLabels();
    if (engine_->active()) {
      engine_->setThreads(threads_->value());
      engine_->setExtraDifficulty(extraDiff_->value());
      engine_->setThrottleUs(throttle_->value());
    } else {
      activeThreads_ = threads_->value();
      activeExtraDiff_ = extraDiff_->value();
      activeThrottle_ = throttle_->value();
      refreshSettingsCards();
    }
  };
  connect(threads_, &QSlider::valueChanged, this, onKnob);
  connect(extraDiff_, &QSlider::valueChanged, this, onKnob);
  connect(throttle_, &QSlider::valueChanged, this, onKnob);
  syncKnobLabels();

  // The credit band: cwb is an agent that keeps you funded. Factory-enabled;
  // below min it mines (hidden cockpit is fine), above max it stops. Values
  // are printed beside the sliders so the range is never blind.
  {
    CwbSettings cfg;
    autoChk_ = new QCheckBox(tr("Auto-mine to keep my balance in a range"));
    autoChk_->setObjectName("advLabel");
    autoChk_->setChecked(cfg.autoMineEnabled());
    adv->addWidget(autoChk_, 3, 0, 1, 3);
    autoMin_ = mkSlider(0, 1000, cfg.autoMineMin());
    autoMax_ = mkSlider(0, 1000, cfg.autoMineMax());
    autoMinVal_ = new QLabel;
    autoMaxVal_ = new QLabel;
    for (QLabel* l : {autoMinVal_, autoMaxVal_}) l->setObjectName("knobVal");
    auto* lmin = new QLabel(tr("Mine below"));
    lmin->setObjectName("advLabel");
    auto* lmax = new QLabel(tr("Stop above"));
    lmax->setObjectName("advLabel");
    adv->addWidget(lmin, 4, 0);
    adv->addWidget(autoMin_, 4, 1);
    adv->addWidget(autoMinVal_, 4, 2);
    adv->addWidget(lmax, 5, 0);
    adv->addWidget(autoMax_, 5, 1);
    adv->addWidget(autoMaxVal_, 5, 2);
    auto syncBand = [this]() {
      autoMinVal_->setText(QString::number(autoMin_->value()) + tr(" cr"));
      autoMaxVal_->setText(QString::number(autoMax_->value()) + tr(" cr"));
      const bool on = autoChk_->isChecked();
      autoMin_->setEnabled(on);
      autoMax_->setEnabled(on);
    };
    // min <= max always: dragging one past the other pushes it along.
    connect(autoMin_, &QSlider::valueChanged, this, [this, syncBand](int v) {
      if (v > autoMax_->value()) autoMax_->setValue(v);
      CwbSettings c;
      c.setAutoMineMin(v);
      syncBand();
    });
    connect(autoMax_, &QSlider::valueChanged, this, [this, syncBand](int v) {
      if (v < autoMin_->value()) autoMin_->setValue(v);
      CwbSettings c;
      c.setAutoMineMax(v);
      syncBand();
    });
    connect(autoChk_, &QCheckBox::toggled, this, [this, syncBand](bool on) {
      CwbSettings c;
      c.setAutoMineEnabled(on);
      autoSuspended_ = false;  // touching the checkbox re-arms the heuristic
      syncBand();
    });
    syncBand();
  }

  root->addStretch();

  auto* foot = new QHBoxLayout;
  startStop_ = new QPushButton;
  foot->addWidget(startStop_);
  foot->addStretch();
  statusLine_ = new QLabel(tr("Idle."));
  statusLine_->setObjectName("status");
  foot->addWidget(statusLine_);
  root->addLayout(foot);
  connect(startStop_, &QPushButton::clicked, this, [this]() {
    // The button is the USER. Manual start: the heuristic must not stop it.
    // Manual stop: the heuristic is parked for this app session (the checkbox
    // in Advanced re-arms it).
    if (state_ == State::Idle) {
      autoSession_ = false;
      autoSuspended_ = false;
    } else {
      autoSuspended_ = true;
    }
    onStartStop();
  });

  uiTimer_ = new QTimer(this);
  uiTimer_->setInterval(1000);
  connect(uiTimer_, &QTimer::timeout, this, &MiningWindow::refreshReadout);

  connect(engine_, &cwb::MinerEngine::status, this,
          [this](const QString& s) { statusLine_->setText(s); });
  connect(engine_, &cwb::MinerEngine::connected, this, [this](int minDiff) {
    serverMinDiff_ = minDiff;
    setState(State::Mining);
  });
  connect(engine_, &cwb::MinerEngine::tick, this,
          [this](quint64 total, double rate) {
            totalHashes_ = total;
            windowHashrate_ = rate;
            if (state_ == State::Starting) setState(State::Mining);
            graph_->addSample(rate);
            refreshReadout();
          });
  connect(engine_, &cwb::MinerEngine::solution, this,
          [this](quint64, quint64 totSol, quint64 totCr) {
            solutions_ = totSol;
            creditUnits_ = totCr;
            refreshReadout();
          });
  connect(engine_, &cwb::MinerEngine::balance, this,
          [this](qint64 units, bool ok) {
            balanceUnits_ = units;
            balanceOk_ = ok;
            refreshReadout();
            if (ok) emit balanceChanged(units);  // keep the status bar in sync
          });
  connect(engine_, &cwb::MinerEngine::activeParams, this,
          [this](int t, int e, int th) {
            activeThreads_ = t;
            activeExtraDiff_ = e;
            activeThrottle_ = th;
            refreshSettingsCards();
            refreshReadout();
          });
  connect(engine_, &cwb::MinerEngine::failed, this, [this](const QString& e) {
    statusLine_->setText(e);
    statusLine_->setStyleSheet("color:#e06c6c;");
    setState(State::Idle);
  });
  connect(engine_, &cwb::MinerEngine::stopped, this, [this]() {
    if (state_ != State::Idle) setState(State::Idle);
  });

  // The service may have been mining long before this optional view existed.
  // Signals emitted before the view was created are intentionally not replayed;
  // active() is sufficient to attach the cockpit to the live session.
  setState(engine_->active() ? State::Mining : State::Idle);
  refreshSettingsCards();
  refreshReadout();
}

void MiningWindow::setExternalBalance(qint64 sumUnits) {
  externalBalance_ = sumUnits;
  refreshReadout();
}

void MiningWindow::setInheritedServer(const QString& server) {
  inheritedServer_ = server;
  if (state_ == State::Idle) target_->setText(server);
}

void MiningWindow::startFromCli(const QString& server) {
  target_->setText(server);
  if (state_ == State::Idle) onStartStop();
}

void MiningWindow::autoStart(const QString& server) {
  if (state_ != State::Idle || autoSuspended_) return;
  target_->setText(server);
  autoSession_ = true;
  onStartStop();
}

void MiningWindow::autoStop() {
  if (!autoSession_) return;  // never stop a session the user started
  if (state_ == State::Mining || state_ == State::Starting) {
    autoSession_ = false;
    onStartStop();
  }
}

void MiningWindow::setState(State s) {
  state_ = s;
  const bool idle = (s == State::Idle);
  // Only the server is locked while mining (changing it needs a reconnect); the
  // threads/difficulty/throttle knobs stay live and editable.
  target_->setEnabled(idle);
  startStop_->setEnabled(s != State::Stopping);

  auto setPill = [this](const QString& t, const QString& bg, const QString& fg) {
    pill_->setText(t);
    pill_->setStyleSheet(QString("background:%1;color:%2;").arg(bg, fg));
  };
  switch (s) {
    case State::Idle:
      startStop_->setText(tr("Start mining"));
      startStop_->setObjectName("go");
      setPill(tr("IDLE"), "#232b36", "#8b93a3");
      target_->setText(inheritedServer_);
      uiTimer_->stop();
      break;
    case State::Starting:
      startStop_->setText(tr("Stop"));
      startStop_->setObjectName("stop");
      setPill(tr("STARTING"), "#3a3320", "#dcb46a");
      break;
    case State::Mining:
      startStop_->setText(tr("Stop"));
      startStop_->setObjectName("stop");
      setPill(tr("MINING"), "#173a26", "#4fd08a");
      statusLine_->setStyleSheet("color:#8b93a3;");
      if (!elapsed_.isValid()) elapsed_.start();
      uiTimer_->start();
      break;
    case State::Stopping:
      startStop_->setText(tr("Stopping..."));
      startStop_->setObjectName("stop");
      setPill(tr("STOPPING"), "#3a3320", "#dcb46a");
      break;
  }
  startStop_->style()->unpolish(startStop_);
  startStop_->style()->polish(startStop_);
}

void MiningWindow::onStartStop() {
  if (state_ == State::Idle) {
    const QString server = target_->text().trimmed();
    if (server.isEmpty()) {
      statusLine_->setText(tr("Enter a server to mine against."));
      return;
    }
    totalHashes_ = 0;
    windowHashrate_ = 0;
    solutions_ = 0;
    creditUnits_ = 0;
    serverMinDiff_ = 0;
    energyKwh_ = 0;
    lastEnergyMs_ = 0;
    graph_->clear();
    elapsed_.restart();
    statusLine_->setStyleSheet("color:#8b93a3;");
    setState(State::Starting);
    engine_->start(server, cwb::loadOrCreateIdentity(), threads_->value(),
                   extraDiff_->value(), throttle_->value());
  } else {
    setState(State::Stopping);
    engine_->stop();
  }
}

void MiningWindow::refreshReadout() {
  // Accumulate energy spent since the last refresh (only while mining). Called
  // often enough (ticks + 1 s timer) for a smooth integral; repeated calls in
  // the same instant add ~0.
  if (state_ == State::Mining && elapsed_.isValid()) {
    const qint64 nowMs = elapsed_.elapsed();
    const double dtHours = (nowMs - lastEnergyMs_) / 1000.0 / 3600.0;
    if (dtHours > 0)
      energyKwh_ += cwb::cpuPowerWatts(activeThreads_, hwThreads_) / 1000.0 * dtHours;
    lastEnergyMs_ = nowMs;
  }

  const double elapsedSec =
      elapsed_.isValid() ? elapsed_.elapsed() / 1000.0 : 0.0;
  const double avgRate = elapsedSec > 0 ? totalHashes_ / elapsedSec : 0.0;
  const int difficulty =
      serverMinDiff_ > 0 ? serverMinDiff_ + activeExtraDiff_ : 0;

  cwb::MiningInputs in;
  in.hashrate = windowHashrate_;
  in.avgHashrate = avgRate;
  in.difficulty = difficulty;
  in.solutions = solutions_;
  in.creditsEarnedUnits = double(creditUnits_);
  in.elapsedSec = elapsedSec;
  const cwb::MiningReadout r = cwb::computeReadout(in);

  vHashrate_->setText(fmtHashrate(windowHashrate_));
  sHashrate_->setText(tr("avg %1").arg(fmtHashrate(avgRate)));
  // Your one account's balance. The engine's own per-solution sample is the
  // freshest source while mining; the browser's periodic poll (externalBalance_)
  // is the fallback when idle.
  const bool haveBal = balanceOk_ || externalBalance_ >= 0;
  const qint64 shownUnits =
      balanceOk_ ? balanceUnits_ : externalBalance_;
  vBalance_->setText(haveBal ? fmtCreditsFull(shownUnits / 1e8)
                             : QStringLiteral("--"));
  const double earnedCredits = creditUnits_ / 1e8;
  const double usdeee = cwb::usdEnergyEquivalent(
      haveBal ? shownUnits / 1e8 : 0.0, energyKwh_, earnedCredits);
  const bool haveCost = balanceOk_ && earnedCredits > 0 && energyKwh_ > 0;
  vUsdeee_->setText(haveCost ? "$" + QString::number(usdeee, 'f', 6)
                             : QStringLiteral("--"));
  sUsdeee_->setText(haveCost
                        ? tr("energy to mine it, @ $%1/kWh")
                              .arg(cwb::kElectricityUsdPerKwh, 0, 'f', 2)
                        : tr("energy-cost equivalent"));
  vEarned_->setText(fmtCreditsFull(creditUnits_ / 1e8));
  sEarned_->setText(r.observedCreditsPerSec > 0
                        ? tr("%1 /s observed").arg(fmtCreditsRate(r.observedCreditsPerSec))
                        : QString());
  vSolutions_->setText(QString::number(solutions_));
  sSolutions_->setText(solutions_ > 0
                           ? tr("~%1 each").arg(fmtDuration(r.observedSecPerSolution))
                           : tr("none yet"));
  vPerDay_->setText(difficulty ? tr("%1 /day").arg(fmtCreditsRate(r.projectedCreditsPerDay))
                               : QStringLiteral("--"));
  sPerHour_->setText(difficulty
                         ? tr("%1 /hr").arg(fmtCreditsRate(r.projectedCreditsPerSec * 3600))
                         : QString());
  vObserved_->setText(
      r.observedCreditsPerSec > 0
          ? tr("%1 /day").arg(fmtCreditsRate(r.observedCreditsPerSec * 86400))
          : QStringLiteral("--"));
  vEta_->setText(r.etaSecToSolution > 0 ? fmtDuration(r.etaSecToSolution)
                                        : QStringLiteral("--"));
  elapsedHead_->setText(elapsedSec > 0 ? fmtDuration(elapsedSec) : QString());
}

void MiningWindow::refreshSettingsCards() {
  vThreadsC_->setText(QString::number(activeThreads_));
  sThreadsC_->setText(tr("of %1 cores")
                          .arg(std::max(1, int(std::thread::hardware_concurrency()))));
  if (serverMinDiff_ > 0) {
    vDifficulty_->setText(QString::number(serverMinDiff_ + activeExtraDiff_));
    sDifficulty_->setText(tr("server %1 + %2").arg(serverMinDiff_).arg(activeExtraDiff_));
  } else {
    vDifficulty_->setText(QStringLiteral("+") + QString::number(activeExtraDiff_));
    sDifficulty_->setText(tr("over server minimum"));
  }
  vThrottleC_->setText(activeThrottle_ > 0
                           ? QString::number(activeThrottle_) + tr(" us")
                           : tr("off"));
}

void MiningWindow::closeEvent(QCloseEvent* e) {
  // Closing the window does not stop mining; it keeps running in the
  // background and reopening shows the same live engine. Hide instead.
  if (state_ == State::Mining || state_ == State::Starting) {
    hide();
    emit cockpitClosed();
    e->ignore();
    return;
  }
  emit cockpitClosed();
  e->accept();
}
