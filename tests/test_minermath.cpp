#include <QtTest>

#include <cmath>

#include "minermath.h"

using namespace cwb;

class MinerMathTest : public QObject {
  Q_OBJECT
 private slots:
  void expectedHashes() {
    QCOMPARE(expectedHashesForDifficulty(0), 0.0);
    QCOMPARE(expectedHashesForDifficulty(1), 2.0);
    QCOMPARE(expectedHashesForDifficulty(10), 1024.0);
    QCOMPARE(expectedHashesForDifficulty(20), 1048576.0);
  }

  void creditScalesWithDifficulty() {
    // (1 << (D-1)) * 1000 / 1e8 whole credits.
    QCOMPARE(creditPerSolution(1), 1000.0 / 1e8);
    QCOMPARE(creditPerSolution(21), std::ldexp(1.0, 20) * 1000.0 / 1e8);
    // Each extra bit doubles the reward.
    QVERIFY(qFuzzyCompare(creditPerSolution(16) * 2.0, creditPerSolution(17)));
  }

  void perHashYieldIsDifficultyIndependent() {
    // The headline invariant: projected credits/sec depends only on hashrate,
    // not difficulty. Same rate at D=12 and D=30 must project identically.
    MiningInputs a{5000, 5000, 12, 0, 0, 10};
    MiningInputs b{5000, 5000, 30, 0, 0, 10};
    const double pa = computeReadout(a).projectedCreditsPerSec;
    const double pb = computeReadout(b).projectedCreditsPerSec;
    QVERIFY(qFuzzyCompare(pa, pb));
    // 5000 H/s * 500 units/hash / 1e8 = 0.025 credits/sec.
    QVERIFY(qFuzzyCompare(pa, 5000.0 * 500.0 / 1e8));
  }

  void etaIsExpectedHashesOverRate() {
    MiningInputs in{1000, 1000, 20, 0, 0, 5};
    const MiningReadout r = computeReadout(in);
    QVERIFY(qFuzzyCompare(r.etaSecToSolution, 1048576.0 / 1000.0));
    QVERIFY(qFuzzyCompare(r.projectedCreditsPerDay,
                          r.projectedCreditsPerSec * 86400.0));
  }

  void idleRateGivesNoEta() {
    MiningInputs in{0, 0, 20, 0, 0, 0};
    const MiningReadout r = computeReadout(in);
    QCOMPARE(r.etaSecToSolution, 0.0);        // no rate -> no projection
    QCOMPARE(r.projectedCreditsPerSec, 0.0);
    QCOMPARE(r.observedCreditsPerSec, 0.0);   // no elapsed -> no observed
  }

  void energyPowerModel() {
    // Half the threads -> half TDP; capped at full TDP; zero guards.
    QVERIFY(qFuzzyCompare(cpuPowerWatts(11, 22), kCpuTdpWatts / 2.0));
    QVERIFY(qFuzzyCompare(cpuPowerWatts(22, 22), kCpuTdpWatts));
    QVERIFY(qFuzzyCompare(cpuPowerWatts(44, 22), kCpuTdpWatts));  // capped
    QCOMPARE(cpuPowerWatts(0, 22), 0.0);
    QCOMPARE(cpuPowerWatts(4, 0), 0.0);
  }

  void usdEnergyEquivalentMath() {
    // 1 kWh spent to earn 10 credits -> $0.15/10 = $0.015 per credit.
    const double cpc = usdPerCredit(1.0, 10.0);
    QVERIFY(qFuzzyCompare(cpc, kElectricityUsdPerKwh / 10.0));
    // A 1000-credit balance is worth 1000 * that.
    QVERIFY(qFuzzyCompare(usdEnergyEquivalent(1000.0, 1.0, 10.0), 1000.0 * cpc));
    // No credits earned yet -> undefined -> 0.
    QCOMPARE(usdPerCredit(1.0, 0.0), 0.0);
    QCOMPARE(usdEnergyEquivalent(1000.0, 1.0, 0.0), 0.0);
  }

  void observedFiguresFromCounters() {
    // 3 solutions, 6e8 units earned over 120 s.
    MiningInputs in{0, 0, 18, 3, 6e8, 120};
    const MiningReadout r = computeReadout(in);
    QVERIFY(qFuzzyCompare(r.observedSecPerSolution, 40.0));       // 120/3
    QVERIFY(qFuzzyCompare(r.observedCreditsPerSec, 6.0 / 120.0)); // 6 credits/120s
  }
};

QTEST_GUILESS_MAIN(MinerMathTest)
#include "test_minermath.moc"
