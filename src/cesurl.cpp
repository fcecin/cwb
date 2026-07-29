#include "cesurl.h"
#include <QDir>
#include <QUrl>

CesUrl parseCesUrl(const QString& text) {
  CesUrl r;
  const QUrl u(text.trimmed(), QUrl::StrictMode);
  if (!u.isValid() || u.scheme().isEmpty()) {
    r.error = "expected scheme://host[:port][/path]";
    return r;
  }
  r.scheme = u.scheme().toLower();
  r.host = u.host();
  r.port = u.port() > 0 ? static_cast<quint16>(u.port()) : 0;
  r.selector = u.userInfo();
  r.path = u.path().isEmpty() ? QStringLiteral("/") : u.path();

  if (r.scheme == QLatin1String("ces")) {
    r.isMain = true;
  } else {
    r.mount = "/ces/" + r.scheme + "/1";
  }

  if (!r.selector.isEmpty()) {
    bool ok = false;
    const quint64 v = r.selector.toULongLong(&ok);
    if (ok) r.pid = v;
  }

  if (r.host.isEmpty()) {
    r.error = "no host";
    return r;
  }
  if (r.port == 0) {  // no port in the address: default it by scheme
    if (r.isMain) {
      r.port = kCesMainPort;
    } else if (r.scheme == QLatin1String("file") ||
               r.scheme == QLatin1String("compute") ||
               r.scheme == QLatin1String("lua")) {
      r.port = kCesRpcPort;
    }
    // luarpc:// stays port 0 when portless: there is no default (each
    // instance leases its own port), so the browser treats it as "list this
    // server's dialable instances" and redirects to the compute directory.
  }
  r.valid = true;
  return r;
}

QString normalizeFileUrlPath(const QString& path) {
  if (path.isEmpty() || path == QLatin1String("/")) return QStringLiteral("/");
  if (path.endsWith('/')) return path + QLatin1String("index.html");
  return path;
}

LinkTarget resolveLink(const QString& href, const QString& scheme,
                       const QString& selector, const QString& host,
                       quint16 port, const QString& dir) {
  LinkTarget t;
  if (href.isEmpty() || href.startsWith('#')) return t;  // in-page anchor
  if (href.startsWith(QLatin1String("http://")) ||
      href.startsWith(QLatin1String("https://"))) {
    t.kind = LinkTarget::Browser;  // the real TCP/IP web
    t.url = href;
    return t;
  }
  if (href.contains(QLatin1String("://"))) {  // a full CES address
    t.kind = LinkTarget::Navigate;
    t.url = href;
    return t;
  }
  if (scheme.isEmpty()) return t;  // relative href but no page base
  QString path = href.startsWith('/') ? href : (dir + "/" + href);
  path = QDir::cleanPath(path);
  QString url = scheme + "://";
  if (!selector.isEmpty()) url += selector + "@";
  url += host;
  if (port) url += ":" + QString::number(port);
  url += path;
  t.kind = LinkTarget::Navigate;
  t.url = url;
  return t;
}
