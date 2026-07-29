#include "names.h"

#include <QCryptographicHash>

namespace cwb {

QString canonicalKeyName(const QString& raw) {
  // Mirror KeyNames::normalize: spaces -> underscores, strip leading/trailing.
  QString s = raw;
  s.replace(QLatin1Char(' '), QLatin1Char('_'));
  int b = 0, e = s.size();
  while (b < e && s.at(b) == QLatin1Char('_')) ++b;
  while (e > b && s.at(e - 1) == QLatin1Char('_')) --e;
  s = s.mid(b, e - b);
  if (s.isEmpty()) return {};
  const QByteArray u = s.toUtf8();
  if (u.size() > 32) return {};              // key_name is 32 bytes
  if (QString::fromUtf8(u) != s) return {};   // valid UTF-8, no drift
  if (s.startsWith('.') || s.startsWith('-')) return {};  // hidden/option-like
  for (const QChar& c : s) {
    if (c.category() == QChar::Other_Control) return {};
    // The reserved set that breaks a URL, a filesystem, or a shell path.
    static const QString bad = QStringLiteral("/\\:*?\"<>|#%&{}[]^~;@=+,`'()$!");
    if (c.unicode() < 128 && bad.contains(c)) return {};
  }
  return s;
}

QString prettyName(const QString& keyname) {
  QString s = keyname;
  s.replace(QLatin1Char('_'), QLatin1Char(' '));
  return s;
}

QString storyFileName(const QString& title, const QByteArray& source) {
  QString kebab;
  bool dash = false;
  for (const QChar& qc : title) {
    const QChar c = qc.toLower();
    if (c.isLetterOrNumber() && c.unicode() < 128) {
      kebab += c;
      dash = false;
    } else if (!kebab.isEmpty() && !dash) {
      kebab += QLatin1Char('-');
      dash = true;
    }
    if (kebab.size() >= 48) break;
  }
  while (kebab.endsWith(QLatin1Char('-'))) kebab.chop(1);
  if (kebab.isEmpty()) kebab = QStringLiteral("untitled");
  const QByteArray h =
      QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex();
  return kebab + QLatin1Char('-') + QString::fromLatin1(h.left(6)) +
         QStringLiteral(".html");
}

}  // namespace cwb
