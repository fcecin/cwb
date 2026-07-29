#pragma once
#include <ces/keys.h>

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>

// MinerEngine: drives a CES RandomX mining loop on a background thread and
// reports live telemetry to the GUI. One engine mines one account against one
// server. Signals are emitted from the worker thread and delivered queued to
// the GUI thread (connect with the default AutoConnection). The full RandomX
// dataset is built on start (needed to hash), so the first tick lags a moment.
namespace cwb {

class MinerEngine : public QObject {
  Q_OBJECT
 public:
  explicit MinerEngine(QObject* parent = nullptr);
  ~MinerEngine() override;

  // Begin mining `key`'s account against `server` ("host" or "host:port"; no
  // port means the default CES main port, where PoW tickets are sent). threads
  // clamps to hardware concurrency inside the worker. extraDifficulty adds to
  // the server minimum; throttleUs sleeps between hash batches (0 = flat out).
  void start(const QString& server, const ces::KeyPair& key, int threads,
             int extraDifficulty, int throttleUs);

  // Signal the worker to wind down. Returns immediately; `stopped` fires when
  // the thread has exited and any lock is released.
  void stop();

  bool active() const { return running_.load(); }

  // Live knobs: adopted by the worker on its next hash batch (a fraction of a
  // second). Safe to call any time; the worker clamps them. It echoes the
  // values it is actually using through activeParams.
  void setThreads(int n) { liveThreads_.store(n); }
  void setExtraDifficulty(int d) { liveExtraDiff_.store(d); }
  void setThrottleUs(int us) { liveThrottle_.store(us); }

 signals:
  void status(const QString& detail);          // human status line
  void connected(int serverMinDifficulty);     // handshake done; server min diff
  void tick(quint64 totalHashes, double windowHashrate);
  void solution(quint64 creditUnits, quint64 totalSolutions,
                quint64 totalCreditUnits);
  void balance(qint64 balanceUnits, bool ok);  // account balance refresh
  // The knob values the worker is actually using now (echoes live changes).
  void activeParams(int threads, int extraDifficulty, int throttleUs);
  void failed(const QString& error);           // fatal: mining cannot proceed
  void stopped();                              // worker exited

 private:
  void run(QString server, ces::KeyPair key);

  std::thread thread_;
  std::atomic<bool> stopFlag_{false};
  std::atomic<bool> running_{false};
  std::atomic<int> liveThreads_{1};
  std::atomic<int> liveExtraDiff_{0};
  std::atomic<int> liveThrottle_{0};
};

}  // namespace cwb
