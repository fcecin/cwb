#pragma once
#include <QString>
#include <memory>

class QSettings;

// Typed wrapper over QSettings for cwb's preferences, with every default in one
// place. The app uses the native store (org "ces", app "cwb"); tests point it at
// an explicit INI file. Widgets-free (cwb_core) so it is unit-tested.
class CwbSettings {
 public:
  CwbSettings();                                 // native per-user store
  explicit CwbSettings(const QString& iniFile);  // explicit file (tests)
  ~CwbSettings();

  double zoom() const;              // default/global page zoom (0.5..3.0)
  void setZoom(double z);
  // Per-host zoom: remembers a zoom for each site, falling back to zoom() for a
  // host with no stored value (empty host -> the global default).
  double zoomForHost(const QString& host) const;
  void setZoomForHost(const QString& host, double z);

  qint64 cacheMaxBytes() const;     // disk cache cap (default 128 MB)
  void setCacheMaxBytes(qint64 bytes);

  QString homePage() const;         // start/home address (default welcome page)
  void setHomePage(const QString& url);

  bool restoreSession() const;      // reopen the last page on launch (default off)
  void setRestoreSession(bool on);
  QString lastUrl() const;          // last navigated address (for restore)
  void setLastUrl(const QString& url);
  QString lastFileServer() const;   // host:rpcport of the last file:// server
  void setLastFileServer(const QString& hostPort);  // the write widget's publish target

  // Auto-mining credit band: keep the bank between [min, max] whole credits.
  // Factory-enabled: below min the browser mines (hidden cockpit), above max
  // it stops. Manual start/stop overrides for the session.
  bool autoMineEnabled() const;     // default true
  void setAutoMineEnabled(bool on);
  int autoMineMin() const;          // whole credits, default 10
  void setAutoMineMin(int credits);
  int autoMineMax() const;          // whole credits, default 20
  void setAutoMineMax(int credits);

  static QString defaultHomePage();

 private:
  std::unique_ptr<QSettings> s_;
};
