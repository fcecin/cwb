#include "minermath.h"

#include <cmath>

namespace cwb {

double expectedHashesForDifficulty(int difficulty) {
  if (difficulty <= 0) return 0;
  return std::ldexp(1.0, difficulty);  // 2^difficulty
}

double creditPerSolution(int difficulty) {
  if (difficulty <= 0) return 0;
  return std::ldexp(1.0, difficulty - 1) * kPowRewardBase / kPriceUnit;
}

double projectedCreditsPerSec(double hashrate) {
  if (hashrate <= 0) return 0;
  // Per-hash yield = kPowRewardBase/2 internal units, difficulty-independent.
  return hashrate * (kPowRewardBase / 2.0) / kPriceUnit;
}

double cpuPowerWatts(int activeThreads, int totalThreads) {
  if (activeThreads <= 0 || totalThreads <= 0) return 0;
  const double frac = std::min(1.0, double(activeThreads) / totalThreads);
  return frac * kCpuTdpWatts;
}

double usdPerCredit(double energyKwh, double creditsEarned) {
  if (creditsEarned <= 0 || energyKwh <= 0) return 0;
  return (energyKwh * kElectricityUsdPerKwh) / creditsEarned;
}

double usdEnergyEquivalent(double balanceCredits, double energyKwh,
                           double creditsEarned) {
  return balanceCredits * usdPerCredit(energyKwh, creditsEarned);
}

MiningReadout computeReadout(const MiningInputs& in) {
  MiningReadout r;
  r.expectedHashesPerSolution = expectedHashesForDifficulty(in.difficulty);
  r.creditPerSolution = creditPerSolution(in.difficulty);
  if (in.hashrate > 0 && r.expectedHashesPerSolution > 0)
    r.etaSecToSolution = r.expectedHashesPerSolution / in.hashrate;
  r.projectedCreditsPerSec = projectedCreditsPerSec(in.hashrate);
  r.projectedCreditsPerDay = r.projectedCreditsPerSec * 86400.0;
  if (in.elapsedSec > 0) {
    r.observedCreditsPerSec = (in.creditsEarnedUnits / kPriceUnit) / in.elapsedSec;
    if (in.solutions > 0)
      r.observedSecPerSolution = in.elapsedSec / static_cast<double>(in.solutions);
  }
  return r;
}

}  // namespace cwb
