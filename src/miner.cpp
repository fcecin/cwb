#include "miner.h"

#include <ces/account.h>
#include <ces/client.h>
#include <ces/types.h>
#include <ces/util/resolver.h>

#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <optional>

namespace cwb {

using clock_t = std::chrono::steady_clock;

MinerEngine::MinerEngine(QObject* parent) : QObject(parent) {}

MinerEngine::~MinerEngine() {
  stopFlag_.store(true);
  if (thread_.joinable()) thread_.join();
}

void MinerEngine::start(const QString& server, const ces::KeyPair& key,
                        int threads, int extraDifficulty, int throttleUs) {
  if (running_.load()) return;
  if (thread_.joinable()) thread_.join();  // reap a finished prior run
  liveThreads_.store(threads);
  liveExtraDiff_.store(extraDifficulty);
  liveThrottle_.store(throttleUs);
  stopFlag_.store(false);
  running_.store(true);
  thread_ = std::thread(&MinerEngine::run, this, server, key);
}

void MinerEngine::stop() { stopFlag_.store(true); }

void MinerEngine::run(QString server, ces::KeyPair key) {
  // Single-miner lock: one cwb process mines at a time, so two windows (or two
  // instances) never double-mine the same account and waste the dataset RAM.
  const QString lockDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(lockDir);
  QLockFile lock(lockDir + "/miner.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) {
    emit failed(tr("Another cwb instance is already mining."));
    running_.store(false);
    emit stopped();
    return;
  }

  try {
    std::string hostPort = server.trimmed().toStdString();
    if (hostPort.find(':') == std::string::npos)
      hostPort += ":" + std::to_string(ces::DEFAULT_PORT);  // main port

    emit status(tr("Resolving %1 ...").arg(QString::fromStdString(hostPort)));
    boost::asio::ip::udp::endpoint ep;
    try {
      ep = ces::Resolver::resolveUdp(hostPort);
    } catch (const std::exception& e) {
      emit failed(tr("Cannot resolve server: %1").arg(e.what()));
      running_.store(false);
      emit stopped();
      return;
    }

    emit status(tr("Building RandomX dataset (this uses ~2 GB) ..."));
    ces::CesClient client(ep, /*useDataset=*/true);
    client.setKey(key);
    client.start(0);
    if (!client.connect()) {
      emit failed(tr("Connect failed."));
      running_.store(false);
      emit stopped();
      return;
    }

    const int serverMinDiff = client.getMinDifficulty();
    emit connected(serverMinDiff);
    emit status(tr("Connected. Mining at difficulty %1.")
                    .arg(serverMinDiff + std::max(0, liveExtraDiff_.load())));

    const minx::Hash pubHash = key.getPublicKeyAsHash();
    const ces::HashPrefix mapKey = ces::Account::getMapKey(pubHash);
    auto queryBalance = [&]() {
      int64_t bal = 0;
      uint32_t nonce = 0;
      const uint8_t rc = client.queryAccount(mapKey, bal, nonce);
      emit balance(bal, rc == ces::CES_OK);
    };
    queryBalance();

    const int hwMax = std::max(1, int(std::thread::hardware_concurrency()));
    uint64_t totalHashes = 0;
    uint64_t totalSolutions = 0;
    uint64_t totalCreditUnits = 0;
    uint64_t nonce = 0;
    int instantFails = 0;
    int lastThreads = -1, lastExtra = -1, lastThrottle = -1;

    auto lastTick = clock_t::now();
    uint64_t hashesAtLastTick = 0;
    auto lastBalance = clock_t::now();

    while (!stopFlag_.load()) {
      // Read the live knobs each batch so the user's Advanced changes take
      // effect on the next cycle; echo what we actually adopt.
      const int nThreads = std::clamp(liveThreads_.load(), 1, hwMax);
      const int extraDifficulty = std::max(0, liveExtraDiff_.load());
      const int throttleUs = std::max(0, liveThrottle_.load());
      if (nThreads != lastThreads || extraDifficulty != lastExtra ||
          throttleUs != lastThrottle) {
        emit activeParams(nThreads, extraDifficulty, throttleUs);
        lastThreads = nThreads;
        lastExtra = extraDifficulty;
        lastThrottle = throttleUs;
      }
      // mine() re-acquires the ticket and rebuilds the hash setup on every call,
      // so a batch must be big enough to amortize that: size it per worker
      // thread (batchSize is the total nonce span the threads share). Throttle
      // mode uses a small batch for fine-grained sleeps.
      const uint64_t batchSize =
          uint64_t(throttleUs > 0 ? 8 : 256) * uint64_t(nThreads);

      const auto t0 = clock_t::now();
      std::optional<minx::MinxProveWork> found = client.mine(
          static_cast<uint8_t>(extraDifficulty), {}, nThreads, nonce, batchSize);
      const double batchSec =
          std::chrono::duration<double>(clock_t::now() - t0).count();

      if (found) {
        instantFails = 0;
        minx::Hash b;
        uint64_t credit = 0, t = 0;
        const int r = client.proveWork(*found, b, credit, t);
        if (r == minx::MINX_SOLUTION_SPENT) {
          totalSolutions++;
          totalCreditUnits += credit;
          emit solution(credit, totalSolutions, totalCreditUnits);
          // Refresh the balance after a solution, but not more than once every
          // few seconds -- queryAccount blocks the hash loop for a round trip.
          if (std::chrono::duration<double>(clock_t::now() - lastBalance)
                  .count() >= 5.0) {
            queryBalance();
            lastBalance = clock_t::now();
          }
        }
        // Untimely/unspent/unknown: silently mine on (the ticket aged out or
        // the reply was lost). The next batch produces a fresh solution.
        nonce += batchSize;
        continue;
      }

      // No solution this batch. A near-instant empty return means no hashing
      // happened (no PoW engine on the server, or the ticket path is closed):
      // count them and give up rather than spin a hot no-op loop.
      if (batchSec < 0.004) {
        if (++instantFails >= 8) {
          emit failed(tr("Server accepted no proof-of-work (no mining engine, "
                         "or unreachable)."));
          break;
        }
      } else {
        // maxIters (batchSize) is the TOTAL nonces tested across all worker
        // threads (they share one nonce counter), not per thread -- so one
        // batch is exactly batchSize hashes regardless of nThreads.
        instantFails = 0;
        totalHashes += batchSize;
      }
      nonce += batchSize;

      const auto now = clock_t::now();
      const double sinceTick =
          std::chrono::duration<double>(now - lastTick).count();
      if (sinceTick >= 0.5) {
        const double windowRate =
            (totalHashes - hashesAtLastTick) / sinceTick;
        emit tick(totalHashes, windowRate);
        lastTick = now;
        hashesAtLastTick = totalHashes;
      }
      if (std::chrono::duration<double>(now - lastBalance).count() >= 20.0) {
        queryBalance();
        lastBalance = now;
      }
      if (throttleUs > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(throttleUs));
    }

    client.disconnect();
    client.stop();
  } catch (const std::exception& e) {
    emit failed(tr("Mining error: %1").arg(e.what()));
  } catch (...) {
    emit failed(tr("Mining error (unknown)."));
  }

  lock.unlock();
  running_.store(false);
  emit stopped();
}

}  // namespace cwb
