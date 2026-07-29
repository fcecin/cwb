#pragma once
#include "cache.h"
#include "cesurl.h"
#include "miner.h"
#include "widget.h"

#include <QDebug>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>

class QAction;
class QByteArray;
class QComboBox;
class QLineEdit;
class QLabel;
class QToolBar;
class QProgressBar;
class QRect;
class QPoint;
class QResizeEvent;
class QCompleter;
class QStringListModel;
class QMenu;
class UrlHistory;
class CwbSettings;
class MiningWindow;
class RenderPane;
class TerminalPane;
class ImagePane;
struct CesUrl;
namespace cwb {
class CesLuaSession;
class FullscreenWidget;
}

class MainWindow : public QMainWindow, public cwb::WidgetContext {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // cwb::WidgetContext: the capability surface for embedded native widgets.
  ces::KeyPair identity() const override;
  ces::KeyPair authorIdentity() const override;
  QString authorName() const override;
  QString authorHandle() const override;
  QString serverHost() const override { return curHost_; }
  // The SERVER's rpc lane. On a luarpc:// page curPort_ is the instance's own
  // endpoint (file/compute binds there NACK), so fall back to the convention;
  // a page that knows better overrides via the widget rpcport param.
  quint16 serverRpcPort() const override {
    if (curScheme_ == QLatin1String("luarpc")) return kCesRpcPort;
    return static_cast<quint16>(curPort_);
  }
  QString currentAddress() const override { return curAddr_; }
  // Post-action navigation must fetch fresh: cache shows the pre-mutation page.
  void navigateTo(const QString& url) override {
    qWarning().noquote() << "cwb-nav: widget navigateTo ->" << url;
    pendingPolicy_ = CachePolicy::BypassDoc;
    openUrl(url);
  }
  // Re-fetch + re-render the current page, cache-bypassed.
  void reloadCurrent() override {
    qWarning().noquote() << "cwb-nav: widget reloadCurrent ->" << curAddr_;
    if (!curAddr_.isEmpty()) { pendingPolicy_ = CachePolicy::BypassDoc; openUrl(curAddr_); }
  }
  void loadHtml(const QString& html);
  void openUrl(const QString& url);
  void findInPage(const QString& query);  // open the find bar, search
  // Open the cockpit and start mining `server` (the --mine launch path).
  void openMiningAndStart(const QString& server);
  // Harness: windowshot must never trigger real background mining.
  void disableAutoMine() { autoMineAllowed_ = false; }
  // Test seams (headless selftests).
  bool testAddressNavIgnoredWhenSameAsCurrent();
  bool testNavigationProgress();

  // Marshaled from the session's io thread; these run on the GUI thread.
  void onSessionOpened(const QString& hello, const QString& err);
  void onSessionOpenedDirect(const QString& err);
  void feedSession(const QString& bytes);
  void sessionClosed(const QString& reason);
  // Marshaled from the file-fetch worker thread.
  void onFileFetched(const QByteArray& bytes, int code, const QString& path,
                     bool fromCache);
  void onResourceFetched(const QString& url, bool isCss, const QByteArray& bytes,
                         int code);
  void onComputeFetched(const QString& html, int code);
  void onAccountFetched(const QString& html, int code);

 private slots:
  void navigate();
  void goBack();
  void goForward();
  void onLinkClicked(const QString& href);
  void onHoverLink(const QString& href);
  void openMining();
  void newWindow();
  void about();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  // How the next/current page load may use the disk cache. Use = normal
  // navigation (back/forward, links, typed): cache hits allowed. BypassDoc =
  // soft reload (F5): re-fetch the document, subresources may still hit.
  // BypassAll = hard reload (Ctrl+F5): re-fetch everything.
  enum class CachePolicy { Use, BypassDoc, BypassAll };

  void loadAddress(const QString& addr, bool record);  // the navigation core
  void reload(bool hard);  // F5 soft / Ctrl+F5 hard
  void setZoom(double z);
  void showFindBar();
  void hideFindBar();
  void doFind(const QString& query);
  void findNextMatch();
  void findPrevMatch();
  void updateFindCount();
  void scrollContentTo(const QRect& docRect);
  void recordHistory(const QString& addr);
  void refreshAddressSuggestions();  // completer/dropdown = bookmarks + history
  void addCurrentBookmark();
  void removeCurrentBookmark();
  void rebuildBookmarksMenu();
  void showContextMenu(const QString& href, const QPoint& globalPos);
  void openPreferences();
  void updateNavActions();
  void beginNavigationProgress();
  void advanceNavigationProgress(int permille);
  void finishNavigationProgress();
  void positionNavigationProgress();
  void resetSession();
  // Install `w` as the central content; the view is swapped only when the next
  // destination is ready, so navigation can't flash.
  void setContent(QWidget* w);
  void setFullscreenContent(cwb::FullscreenWidget* page);
  void loadError(const QString& title, const QString& url, const QString& detail);
  // Fail-fast: after a fixed timeout show "server not responding" unless a
  // reply rendered or a newer navigation started.
  void armNavDeadline(const QString& addr);
  void buildTerminal(const QString& initial);
  // Direct (luarpc) probe: spoke first -> terminal; silent -> HTTP request.
  void enterDirectTerminal();
  void directProbeTimeout();
  void navigateFile(const CesUrl& u, const QString& addr);
  void navigateCompute(const CesUrl& u, const QString& addr);
  void navigateApplicationDirectory(const QString& host, quint16 rpcPort,
                                    bool record, bool discoverRpc = false);
  void navigateAccount(const CesUrl& u, const QString& addr);  // ces:// main-port
  void saveDownload(const QString& path, const QByteArray& bytes);
  // Streamed save-to-disk for Download-kind files (never buffered in RAM).
  void startDownload(const QString& host, quint16 port, const QString& path,
                     const QString& addr);
  // Progressive sub-resources: CSS/images one at a time, relayout after each.
  void startSubresourceLoad();
  void loadNextResource();
  QString resolveResourcePath(const QString& url) const;
  void markVisitedLinks(const QString& html);

  QAction* backAction_ = nullptr;
  QAction* fwdAction_ = nullptr;
  QComboBox* addressBar_ = nullptr;
  QToolBar* addressToolBar_ = nullptr;
  QProgressBar* navigationProgress_ = nullptr;
  QTimer* navigationProgressTimer_ = nullptr;
  int navigationProgressSerial_ = 0;
  int navigationProgressCeiling_ = 780;
  bool navigationProgressActive_ = false;
  TerminalPane* activeTerminal_ = nullptr;  // set iff the current content is a terminal
  RenderPane* activeRender_ = nullptr;      // set iff the current content is a page
  cwb::FullscreenWidget* activeFullscreen_ = nullptr;
  ImagePane* activeImage_ = nullptr;        // set iff the current content is an image
  std::shared_ptr<cwb::CesLuaSession> session_;
  int sessionMode_ = 0;  // 0 pending, 1 http, 2 terminal, 3 pending-direct
  QString httpAccum_;
  QString curAddr_;
  QString curScheme_;           // current page scheme, for resolving relative links
  QString curSelector_;         // current page userinfo (pid), for relative links
  QString curHost_;
  QString curPath_;
  int curPort_ = 0;
  QString curDir_;              // page directory, for resolving relative urls
  // Auto-miner: below the band start a hidden session, above it stop one WE
  // started. See CwbSettings::autoMine*.
  QTimer* autoMineTimer_ = nullptr;
  bool autoMineAllowed_ = true;
  cwb::MinerEngine minerEngine_;
  bool autoMiningSession_ = false;
  QString autoMiningTarget_;  // sticky server for the current automatic run
  void autoMineTick();
  void onAutoMineBalance(qint64 sumUnits, qint64 bandUnits, bool ok,
                         const QString& target, qint64 sweptUnits);
  // The current server's /account URL; "" when no server is loaded.
  QString accountUrl() const;
  void wireMiningBalance();
  // Status corner: [cockpit button][identity][balance], fixed widths.
  class QToolButton* statusMine_ = nullptr;
  class QToolButton* statusId_ = nullptr;
  class QLabel* statusBal_ = nullptr;
  qint64 statusBalUnits_ = -1;   // <0 = unknown
  void refreshStatusCorner();    // cheap 2s repaint (no networking)
  // Bumped per navigation; an async result with a stale gen is dropped.
  // navSettled_ gates the fail-fast deadline.
  int navGen_ = 0;
  bool navSettled_ = true;
  // Bare-host landing fallback if the cwb entry point 404s. One-shot.
  QString pendingFallback_;
  QSet<QString> resAttempted_;  // sub-resource urls already fetched this page
  QStringList history_;         // visited addresses (back/forward stack)
  int histIndex_ = -1;          // current position in history_
  QSet<QString> visited_;       // full urls navigated to (for visited-link purple)
  double zoom_ = 1.0;           // page zoom, carried across navigations
  QString pendingAnchor_;       // #fragment to scroll to after the next render
  std::unique_ptr<UrlHistory> urlHistory_;   // persistent visited-address list
  QStringListModel* addrModel_ = nullptr;    // completer model (history entries)
  QCompleter* addrCompleter_ = nullptr;      // address-bar autocomplete
  std::unique_ptr<UrlHistory> bookmarks_;    // persistent user-curated addresses
  QMenu* bookmarksMenu_ = nullptr;           // rebuilt from bookmarks_ on show
  std::unique_ptr<CwbSettings> settings_;    // persistent preferences (QSettings)
  QToolBar* findToolBar_ = nullptr;  // bottom find-in-page bar (hidden by default)
  QLineEdit* findEdit_ = nullptr;
  QLabel* findCount_ = nullptr;
  MiningWindow* miningWindow_ = nullptr;             // lazily created, top-level
  QString lastServer_ = QStringLiteral("ces.pubcom.org");  // for the miner target
  // Disk cache of file:// content; shared_ptr so fetch workers outliving the
  // window keep a valid instance.
  std::shared_ptr<cwb::DiskCache> cache_;
  CachePolicy pendingPolicy_ = CachePolicy::Use;  // set by reload, one-shot
  CachePolicy pagePolicy_ = CachePolicy::Use;     // the current page's policy
};
