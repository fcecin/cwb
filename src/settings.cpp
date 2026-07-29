#include "settings.h"

#include <QSettings>
#include <algorithm>

namespace {
constexpr double kDefaultZoom = 1.0;
constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 3.0;
constexpr qint64 kDefaultCacheBytes = 128LL * 1024 * 1024;
}  // namespace

CwbSettings::CwbSettings() : s_(std::make_unique<QSettings>()) {}

CwbSettings::CwbSettings(const QString& iniFile)
    : s_(std::make_unique<QSettings>(iniFile, QSettings::IniFormat)) {}

CwbSettings::~CwbSettings() = default;

double CwbSettings::zoom() const {
  const double z = s_->value(QStringLiteral("view/zoom"), kDefaultZoom).toDouble();
  return std::clamp(z, kMinZoom, kMaxZoom);
}

void CwbSettings::setZoom(double z) {
  s_->setValue(QStringLiteral("view/zoom"), std::clamp(z, kMinZoom, kMaxZoom));
}

double CwbSettings::zoomForHost(const QString& host) const {
  if (host.isEmpty()) return zoom();
  const QString key = QStringLiteral("zoomByHost/") + host;
  if (!s_->contains(key)) return zoom();
  return std::clamp(s_->value(key).toDouble(), kMinZoom, kMaxZoom);
}

void CwbSettings::setZoomForHost(const QString& host, double z) {
  if (host.isEmpty()) {
    setZoom(z);
    return;
  }
  s_->setValue(QStringLiteral("zoomByHost/") + host,
               std::clamp(z, kMinZoom, kMaxZoom));
}

qint64 CwbSettings::cacheMaxBytes() const {
  const qint64 b =
      s_->value(QStringLiteral("cache/maxBytes"), kDefaultCacheBytes).toLongLong();
  return b > 0 ? b : kDefaultCacheBytes;
}

void CwbSettings::setCacheMaxBytes(qint64 bytes) {
  s_->setValue(QStringLiteral("cache/maxBytes"),
               bytes > 0 ? bytes : kDefaultCacheBytes);
}

QString CwbSettings::defaultHomePage() {
  // The CES main-protocol root is the server's capability directory.
  return QStringLiteral("ces://ces.pubcom.org/");
}

QString CwbSettings::homePage() const {
  const QString h =
      s_->value(QStringLiteral("browser/homePage"), defaultHomePage()).toString();
  // Migrate the former factory default. It was a bare-host command meaning
  // "discover applications", so retaining it would make Home and Apps aliases
  // forever for every existing profile.
  if (h == QLatin1String("ces.pubcom.org") ||
      h == QLatin1String("file://ces.pubcom.org/"))
    return defaultHomePage();
  return h.isEmpty() ? defaultHomePage() : h;
}

void CwbSettings::setHomePage(const QString& url) {
  s_->setValue(QStringLiteral("browser/homePage"), url);
}

bool CwbSettings::restoreSession() const {
  return s_->value(QStringLiteral("browser/restoreSession"), false).toBool();
}

void CwbSettings::setRestoreSession(bool on) {
  s_->setValue(QStringLiteral("browser/restoreSession"), on);
}

QString CwbSettings::lastUrl() const {
  return s_->value(QStringLiteral("browser/lastUrl")).toString();
}

void CwbSettings::setLastUrl(const QString& url) {
  s_->setValue(QStringLiteral("browser/lastUrl"), url);
}

QString CwbSettings::lastFileServer() const {
  return s_->value(QStringLiteral("browser/lastFileServer")).toString();
}

void CwbSettings::setLastFileServer(const QString& hostPort) {
  s_->setValue(QStringLiteral("browser/lastFileServer"), hostPort);
}

bool CwbSettings::autoMineEnabled() const {
  return s_->value(QStringLiteral("miner/autoEnabled"), true).toBool();
}

void CwbSettings::setAutoMineEnabled(bool on) {
  s_->setValue(QStringLiteral("miner/autoEnabled"), on);
}

int CwbSettings::autoMineMin() const {
  return s_->value(QStringLiteral("miner/autoMin"), 10).toInt();
}

void CwbSettings::setAutoMineMin(int credits) {
  s_->setValue(QStringLiteral("miner/autoMin"), credits);
}

int CwbSettings::autoMineMax() const {
  return s_->value(QStringLiteral("miner/autoMax"), 20).toInt();
}

void CwbSettings::setAutoMineMax(int credits) {
  s_->setValue(QStringLiteral("miner/autoMax"), credits);
}
