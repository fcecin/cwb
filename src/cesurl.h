#pragma once
#include <QString>

// A CES address. The scheme names a CesPlex mount:
//   lua://    -> /ces/lua/1      luarpc:// -> /ces/luarpc/1
//   compute://-> /ces/compute/1  file://   -> /ces/file/1
// The special scheme "ces" names the main UDP protocol, not a CesPlex mount.
// The URI userinfo slot carries an optional routing selector (a compute pid):
//   lua://<pid>@<host>:<port>/<path>
struct CesUrl {
  QString scheme;    // lowercased scheme
  QString mount;     // "/ces/<scheme>/1"; empty when isMain
  QString host;
  quint16 port = 0;
  QString selector;  // raw userinfo (pid or name)
  quint64 pid = 0;   // selector as uint when numeric, else 0
  QString path;      // protocol route, always starts with "/"
  bool isMain = false;
  bool valid = false;
  QString error;
};

// The CES port conventions (mirrors ces types.h): the main UDP port, and the
// conventional CesPlex rpc port = main + 1. An address that names no port gets
// the right one for its scheme: ces:// -> the main port; file:// compute://
// lua:// -> the rpc port. luarpc:// has no default -- each instance leases its
// own port, so a portless luarpc address is invalid.
constexpr quint16 kCesMainPort = 53830;
constexpr quint16 kCesRpcPort = 53831;

CesUrl parseCesUrl(const QString& text);

// A resolved anchor-click target: open externally in the OS browser, navigate
// within cwb, or do nothing.
struct LinkTarget {
  enum Kind { None, Browser, Navigate };
  Kind kind = None;
  QString url;
};

// 1996 web-server defaulting for file:// paths: a path ending in '/' means
// "<path>index.html" (the L2 file store has no directories; exact paths only).
// "/" stays "/" -- the browser renders its own zones page there, since the
// store's root is not a readable name.
QString normalizeFileUrlPath(const QString& path);

// Resolve an anchor href clicked on a page addressed as
// scheme://[selector@]host[:port]/<path>, with `dir` = the page's directory.
// Real-web hrefs (http/https) -> Browser (a network cwb has no transport for);
// a full CES url, or a relative one resolved against the page -> Navigate;
// a fragment or empty href -> None.
LinkTarget resolveLink(const QString& href, const QString& scheme,
                       const QString& selector, const QString& host,
                       quint16 port, const QString& dir);
