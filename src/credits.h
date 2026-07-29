#pragma once
#include <QChar>
#include <QString>
#include <QStringView>

// One credit = 1e8 internal units (CES PRICE_UNIT). Every user-facing credit
// display uses the canonical "1.00000000" notation via creditsText, and every
// credit input field goes back to raw units via parseCredits. Both are
// integer-exact: a double round-trip silently corrupts large amounts.
namespace cwb {

constexpr quint64 kCreditUnit = 100000000ULL;

// Raw internal units -> "1.00000000" (always 8 decimals, sign preserved).
inline QString creditsText(qint64 units) {
  const bool neg = units < 0;
  const quint64 a =
      neg ? quint64(0) - quint64(units) : static_cast<quint64>(units);
  return QStringLiteral("%1%2.%3")
      .arg(neg ? QStringLiteral("-") : QString())
      .arg(a / kCreditUnit)
      .arg(a % kCreditUnit, 8, 10, QLatin1Char('0'));
}

// "1.5", "0.00000001", ".5", "3" (credits) -> raw units, exactly. Rejects
// empties, signs, junk, more than 8 decimals, second dots, and overflow.
inline bool parseCredits(QStringView s, quint64& outUnits) {
  s = s.trimmed();
  if (s.isEmpty()) return false;
  quint64 whole = 0, frac = 0;
  int fracDigits = -1;  // -1: still in the whole part
  int digits = 0;
  for (const QChar c : s) {
    if (c == QLatin1Char('.')) {
      if (fracDigits >= 0) return false;  // second dot
      fracDigits = 0;
      continue;
    }
    if (!c.isDigit() || c.unicode() > '9') return false;  // ASCII digits only
    const quint64 d = static_cast<quint64>(c.unicode() - '0');
    ++digits;
    if (fracDigits < 0) {
      if (whole > (Q_UINT64_C(0xFFFFFFFFFFFFFFFF) - d) / 10) return false;
      whole = whole * 10 + d;
    } else {
      if (++fracDigits > 8) return false;
      frac = frac * 10 + d;
    }
  }
  if (digits == 0) return false;  // "." alone
  for (int i = fracDigits < 0 ? 0 : fracDigits; i < 8; ++i) frac *= 10;
  if (whole > (Q_UINT64_C(0xFFFFFFFFFFFFFFFF) - frac) / kCreditUnit)
    return false;
  outUnits = whole * kCreditUnit + frac;
  return true;
}

}  // namespace cwb
