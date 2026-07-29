#pragma once
#include <QtGlobal>

// Mining arithmetic, isolated from Qt widgets and the network so it can be
// unit-tested. Every projected figure the cockpit shows is derived here from
// measured inputs (hash count, elapsed time, difficulty) and the server's own
// reward rule -- nothing is fitted.
namespace cwb {

// Protocol constants, mirrored locally so cwb_core stays Qt-only (no ceslib
// include). PRICE_UNIT is ces/types.h; POW_REWARD_BASE is server.cpp. A solution
// at difficulty D mints (1 << (D-1)) * kPowRewardBase internal units, and the
// expected number of hashes to find one is 2^D -- so the expected yield per hash
// is (2^(D-1) * base) / 2^D = base/2 units, independent of difficulty.
constexpr double kPriceUnit = 100000000.0;  // 1e8 internal units per credit
constexpr double kPowRewardBase = 1000.0;

struct MiningInputs {
  double hashrate = 0;             // recent H/s, for ETA + projection
  double avgHashrate = 0;          // lifetime H/s
  int difficulty = 0;              // total target = server min + extra
  quint64 solutions = 0;           // observed accepted solutions
  double creditsEarnedUnits = 0;   // observed, internal units
  double elapsedSec = 0;
};

struct MiningReadout {
  double expectedHashesPerSolution = 0;  // 2^difficulty
  double etaSecToSolution = 0;           // expectedHashes / hashrate (inf if idle)
  double creditPerSolution = 0;          // whole credits for one solution at D
  double projectedCreditsPerSec = 0;     // hashrate-derived, whole credits
  double projectedCreditsPerDay = 0;
  double observedCreditsPerSec = 0;      // earned / elapsed
  double observedSecPerSolution = 0;     // elapsed / solutions
};

// 2^difficulty as a double (exact for difficulty < 1024). 0 for D <= 0.
double expectedHashesForDifficulty(int difficulty);

// Whole credits minted by one accepted solution at `difficulty` (before any
// account-creation fee the server may deduct on a brand-new account).
double creditPerSolution(int difficulty);

// Expected whole credits earned per second at `hashrate`, from the constant
// per-hash yield -- difficulty cancels out.
double projectedCreditsPerSec(double hashrate);

MiningReadout computeReadout(const MiningInputs& in);

// Energy-cost model ("USD energy-equivalent"). Deliberate estimates: real TDP
// and live electricity prices are not portably available; tune per release.
constexpr double kCpuTdpWatts = 65.0;           // whole-CPU draw at full load
constexpr double kElectricityUsdPerKwh = 0.15;  // world residential average

// Estimated CPU power mining with `activeThreads` of `totalThreads`: a linear
// fraction of TDP (idle draw and non-linear frequency scaling ignored). Capped
// at full TDP.
double cpuPowerWatts(int activeThreads, int totalThreads);

// USD cost of one credit given the energy spent (kWh) and the credits earned
// for it. 0 if no credits yet (undefined, converges as the session runs).
double usdPerCredit(double energyKwh, double creditsEarned);

// The headline number: USD of electricity equivalent to a credit balance --
// balance * usdPerCredit(energy, earned).
double usdEnergyEquivalent(double balanceCredits, double energyKwh,
                           double creditsEarned);

}  // namespace cwb
