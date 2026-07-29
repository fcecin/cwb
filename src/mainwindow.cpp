#include "mainwindow.h"
#include "miningwindow.h"
#include "renderpane.h"
#include "cesurl.h"
#include "cesdial.h"
#include "credits.h"
#include "cesidentity.h"
#include "identityreg.h"
#include "names.h"
#include <ces/account.h>
#include <ces/l2/compute_client.h>
#include <ces/util/resolver.h>
#include <ces/util/wallet.h>
#include "terminalpane.h"
#include "imagepane.h"
#include "mediapane.h"
#include "filekind.h"
#include "url_history.h"
#include "settings.h"
#include "codehighlight.h"
#include "csvtable.h"

#include <ces/types.h>
#include <md4c-html.h>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDebug>
#include <QCompleter>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeySequence>
#include <QPointer>
#include <QProgressBar>
#include <QRect>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShortcut>
#include <algorithm>
#include <string>
#include <thread>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringListModel>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

namespace {

class FullscreenPageHost : public QScrollArea {
 public:
  explicit FullscreenPageHost(cwb::FullscreenWidget* page, QWidget* parent)
      : QScrollArea(parent), page_(page) {
    setWidgetResizable(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(
        "QScrollArea{border:1px solid #8b929c;background:#ffffff;}");
    setWidget(page_);
    chrome_ = page_->viewportChrome();
    if (chrome_) {
      // Chrome overlays the viewport (fixed app chrome); the scrollbar keeps
      // the full viewport height.
      chrome_->setParent(viewport());
      chrome_->show();
      chrome_->raise();
    }
    connect(page_, &cwb::FullscreenWidget::contentSizeChanged, this,
            [this] { relayoutPage(); });
    QTimer::singleShot(0, this, [this] { relayoutPage(); });
  }

 protected:
  void resizeEvent(QResizeEvent* e) override {
    QScrollArea::resizeEvent(e);
    relayoutPage();
  }

 private:
  void relayoutPage() {
    if (!page_) return;
    const int w = std::max(1, viewport()->width());
    page_->setFixedWidth(w);
    page_->adjustSize();
    const int h = std::max(viewport()->height(), page_->sizeHint().height());
    page_->resize(w, h);
    if (chrome_) {
      chrome_->setGeometry(0, 0, w, chrome_->height());
      chrome_->raise();
    }
  }

  cwb::FullscreenWidget* page_ = nullptr;
  QWidget* chrome_ = nullptr;
};

std::optional<cwb::WidgetParams> fullscreenWidgetFromHtml(const QString& html) {
  static const QRegularExpression objectRe(
      QStringLiteral("<object\\b([^>]*)>([\\s\\S]*?)</object>"),
      QRegularExpression::CaseInsensitiveOption);
  auto it = objectRe.globalMatch(html);
  if (!it.hasNext()) return std::nullopt;
  const auto object = it.next();
  if (it.hasNext()) return std::nullopt;  // an application proxy is the sole object

  cwb::WidgetParams p;
  const QString attrs = object.captured(1);
  static const QRegularExpression typeRe(
      QStringLiteral("\\btype\\s*=\\s*([\"'])(.*?)\\1"),
      QRegularExpression::CaseInsensitiveOption);
  const auto type = typeRe.match(attrs);
  if (!type.hasMatch()) return std::nullopt;
  p.type = type.captured(2);

  bool declaredFullscreen = p.type == QLatin1String("application/x-cwb-write");
  static const QRegularExpression paramRe(
      QStringLiteral("<param\\b([^>]*)>"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression attrRe(
      QStringLiteral("\\b(name|value)\\s*=\\s*([\"'])(.*?)\\2"),
      QRegularExpression::CaseInsensitiveOption);
  auto params = paramRe.globalMatch(object.captured(2));
  while (params.hasNext()) {
    QString name, value;
    auto attrsIt = attrRe.globalMatch(params.next().captured(1));
    while (attrsIt.hasNext()) {
      const auto a = attrsIt.next();
      if (a.captured(1).compare(QLatin1String("name"), Qt::CaseInsensitive) == 0)
        name = a.captured(3);
      else
        value = a.captured(3);
    }
    if (!name.isEmpty()) p.params[name] = value;
    if (name == QLatin1String("display") && value == QLatin1String("fullscreen"))
      declaredFullscreen = true;
  }
  if (!declaredFullscreen || !cwb::WidgetRegistry::instance().has(p.type))
    return std::nullopt;
  return p;
}

// Gray chrome; true white is reserved for the page. Type-scoped so RenderPane
// and TerminalPane keep their own styles.
const char* kChromeStyle = R"CSS(
QMainWindow { background:#dfe3e9; }
QMenuBar { background:#e4e7ec; color:#2b2f36; border-bottom:1px solid #c2c7d0; }
QMenuBar::item { padding:4px 10px; background:transparent; }
QMenuBar::item:selected { background:#d2d7df; }
QMenu { background:#f2f4f7; color:#2b2f36; border:1px solid #c2c7d0; }
QMenu::item { padding:4px 22px; }
QMenu::item:selected { background:#d7deea; }
QToolBar { background:#dfe3e9; border:none; border-bottom:1px solid #c2c7d0; padding:4px 8px; spacing:6px; }
QStatusBar { background:#d7dbe2; border-top:1px solid #bcc2cc; color:#3a3f46; }
QStatusBar::item { border:none; }
QComboBox { background:#ffffff; border:1px solid #b7bdc7; border-radius:5px; padding:4px 8px; min-height:22px; color:#20242a; }
QComboBox:focus { border:1px solid #5b8def; }
QComboBox QAbstractItemView { background:#ffffff; border:1px solid #b7bdc7; selection-background-color:#d7deea; selection-color:#20242a; }
QToolButton { background:#eceff3; border:1px solid #b7bdc7; border-radius:5px; padding:5px 14px; color:#20242a; }
QToolButton:hover { background:#e1e5ec; }
QToolButton:pressed { background:#d3d8e1; }
)CSS";

QString htmlEscape(const QString& s) {
  QString o = s;
  o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
  return o;
}

// Local error page: glyph, address tried, raw failure detail.
QString errorPageHtml(const QString& title, const QString& url,
                      const QString& detail, const QString& home) {
  // ces:// is a main-port query; every other scheme rides CesPlex.
  const QString via = url.startsWith(QLatin1String("ces://"))
                          ? QStringLiteral("the main CES port")
                          : QStringLiteral("CesPlex");
  return QStringLiteral(R"HTML(<!doctype html><html><head><meta charset=utf-8><style>
body{margin:0;background:#f6f7f9;color:#2b2f36;font-family:sans-serif}
#w{max-width:620px;margin:60px auto;padding:0 28px}
.big{font-size:46px;margin:0;color:#c98a2b}
h1{font-size:22px;font-weight:bold;margin:8px 0 2px}
p{color:#5b6371;font-size:14px;line-height:1.5;margin:8px 0}
.u{font-family:monospace;font-size:13px;background:#eceef1;border:1px solid #d9dde3;border-radius:6px;padding:9px 11px;color:#333;word-break:break-all}
.d{font-family:monospace;font-size:13px;color:#8a3b3b;background:#fbeded;border:1px solid #f0d6d6;border-radius:6px;padding:9px 11px;margin-top:14px;word-break:break-all}
</style></head><body><div id=w>
<div class=big>&#9888;</div>
<h1>%1</h1>
<p>cwb could not reach this address over %5:</p>
<div class=u>%2</div>
<div class=d>%3</div>
<p style="margin-top:18px"><a href="%4">Go home</a></p>
</div></body></html>)HTML")
      .arg(htmlEscape(title), htmlEscape(url), htmlEscape(detail),
           htmlEscape(home), via);
}

// file://host:port/ front door: zones are non-enumerable, so explain the four
// and link the one enumerable index, /s/index.html.
QString fileZonesPageHtml(const QString& host, quint16 port) {
  const QString hp = host + ":" + QString::number(port);
  return QStringLiteral(R"HTML(<!doctype html><html><head><meta charset=utf-8><style>
body{margin:0;background:#ffffff;color:#2b2f36;font-family:sans-serif}
#w{max-width:640px;margin:56px auto;padding:0 28px}
h1{font-size:22px;margin:0 0 2px}
.sub{color:#6b7280;font-size:13px;margin-bottom:20px}
p{color:#5b6371;font-size:14px;line-height:1.55}
a{color:#1a8917}
table{border-collapse:collapse;font-size:14px;margin:14px 0}
td{padding:7px 12px 7px 0;vertical-align:top}
code{font-family:monospace;background:#f0f2f5;border:1px solid #e2e5ea;border-radius:5px;padding:2px 7px;color:#555;white-space:nowrap}
.d{color:#6b7280}
</style></head><body><div id=w>
<h1>File store</h1>
<div class=sub>host <code>%1</code></div>
<p>This server's file store has no root listing: paths are capabilities, and
only the operator zone is enumerable. Files live in four zones:</p>
<table>
<tr><td><a href="/s/index.html">/s/</a></td><td class=d>operator-deployed files; <a href="/s/index.html">browse the catalog</a></td></tr>
<tr><td><code>/p/&lt;path&gt;</code></td><td class=d>public zone, first-come-first-served paths</td></tr>
<tr><td><code>/h/&lt;pubkey&gt;/&lt;path&gt;</code></td><td class=d>a signer's home zone (64-hex account key)</td></tr>
<tr><td><code>/f/&lt;name&gt;/&lt;path&gt;</code></td><td class=d>named zones, gated by key_name or asset ownership</td></tr>
</table>
<h2 style="font-size:16px;margin:26px 0 6px">Manage files</h2>
<div class=sub>signed by your browser identity; billed to your account</div>
<object type="application/x-cwb-files" width="580" height="360"></object>
<p>To read a file, put its exact path in the address:
<code>file://%1/s/index.html</code></p>
</div></body></html>)HTML")
      .arg(htmlEscape(hp));
}

void mdSink(const MD_CHAR* text, MD_SIZE size, void* ud) {
  static_cast<std::string*>(ud)->append(text, static_cast<size_t>(size));
}

// Markdown -> styled HTML via md4c (GFM). Empty result falls back to raw text
// so a .md never blanks.
QString mdToStyledHtml(const QByteArray& md) {
  std::string frag;
  md_html(md.constData(), static_cast<MD_SIZE>(md.size()), &mdSink, &frag,
          MD_DIALECT_GITHUB, 0);
  if (frag.empty()) {
    QString raw = QString::fromUtf8(md);
    raw.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return QStringLiteral("<!doctype html><html><body style='margin:16px'>"
                          "<pre style='white-space:pre-wrap'>%1</pre></body></html>")
        .arg(raw);
  }
  static const char* kCss =
      "body.md{font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,"
      "sans-serif;max-width:820px;margin:24px auto;padding:0 20px;color:#1a1d22;"
      "line-height:1.6;background:#ffffff;}"
      ".md h1,.md h2,.md h3,.md h4{line-height:1.25;margin:24px 0 12px;"
      "font-weight:600;}"
      ".md h1{font-size:28px;border-bottom:1px solid #e2e6ea;padding-bottom:6px;}"
      ".md h2{font-size:22px;border-bottom:1px solid #e2e6ea;padding-bottom:5px;}"
      ".md h3{font-size:18px;}.md h4{font-size:16px;}"
      ".md p{margin:12px 0;}.md a{color:#1a8917;}"
      ".md code{font-family:monospace;background:#f2f4f7;padding:2px 5px;"
      "border-radius:4px;font-size:90%;}"
      ".md pre{background:#f6f8fa;border:1px solid #e2e6ea;border-radius:6px;"
      "padding:12px;overflow:auto;}.md pre code{background:transparent;padding:0;}"
      ".md blockquote{margin:12px 0;padding:0 14px;color:#586069;"
      "border-left:4px solid #d0d7de;}"
      ".md table{border-collapse:collapse;margin:14px 0;}"
      ".md th,.md td{border:1px solid #d0d7de;padding:6px 12px;}"
      ".md th{background:#f2f4f7;}"
      ".md ul,.md ol{margin:12px 0;padding-left:28px;}.md li{margin:4px 0;}"
      ".md hr{border:none;border-top:1px solid #e2e6ea;margin:20px 0;}"
      ".md img{max-width:100%;}";
  return QStringLiteral("<!doctype html><html><head><meta charset=utf-8><style>"
                        "%1</style></head><body class=md>%2</body></html>")
      .arg(QString::fromUtf8(kCss))
      .arg(QString::fromUtf8(frag.data(), int(frag.size())));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // Preferences first: they size the cache and set the initial zoom.
  settings_ = std::make_unique<CwbSettings>();
  cache_ = std::make_shared<cwb::DiskCache>(cwb::DiskCache::defaultDir(),
                                            settings_->cacheMaxBytes());
  zoom_ = settings_->zoom();
  setWindowTitle("cwb");
  resize(1000, 700);
  setStyleSheet(kChromeStyle);

  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(tr("New &Window"), this, &MainWindow::newWindow);
  fileMenu->addAction(tr("&Home"), this, [this]() {
    openUrl(settings_ ? settings_->homePage() : QStringLiteral("ces.pubcom.org"));
  });
  fileMenu->addAction(tr("Open &work folder"), this, []() {
    const QString wdir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/work");
    QDir().mkpath(wdir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(wdir));
  });
  fileMenu->addAction(tr("&Mining..."), this, &MainWindow::openMining);
  fileMenu->addSeparator();
  fileMenu->addAction(tr("&Clear cache"), this, [this]() {
    if (cache_) cache_->clear();
    statusBar()->showMessage(tr("cache cleared"));
  });
  fileMenu->addAction(tr("Clear &history"), this, [this]() {
    if (urlHistory_) urlHistory_->clear();
    refreshAddressSuggestions();  // keep bookmarks in the suggestions
    statusBar()->showMessage(tr("history cleared"));
  });
  fileMenu->addAction(tr("Set current page as &home"), this, [this]() {
    if (settings_ && !curAddr_.isEmpty()) {
      settings_->setHomePage(curAddr_);
      statusBar()->showMessage(tr("home page set to %1").arg(curAddr_));
    }
  });
  fileMenu->addSeparator();
  fileMenu->addAction(tr("&Preferences..."), this, &MainWindow::openPreferences);
  fileMenu->addSeparator();
  fileMenu->addAction(tr("&Quit"), this, &QWidget::close);

  bookmarksMenu_ = menuBar()->addMenu(tr("&Bookmarks"));
  connect(bookmarksMenu_, &QMenu::aboutToShow, this,
          &MainWindow::rebuildBookmarksMenu);

  QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(tr("&About"), this, &MainWindow::about);

  QToolBar* bar = addToolBar(tr("Address"));
  addressToolBar_ = bar;
  bar->setMovable(false);
  backAction_ = bar->addAction(tr("<"), this, &MainWindow::goBack);
  backAction_->setToolTip(tr("Back"));
  backAction_->setEnabled(false);
  fwdAction_ = bar->addAction(tr(">"), this, &MainWindow::goForward);
  fwdAction_->setToolTip(tr("Forward"));
  fwdAction_->setEnabled(false);
  auto* reloadAction = bar->addAction(tr("Reload"), this, [this]() { reload(false); });
  reloadAction->setShortcut(QKeySequence(Qt::Key_F5));
  reloadAction->setToolTip(tr("Reload (F5)"));
  auto* homeAction = bar->addAction(tr("Home"), this, [this]() {
    if (settings_) openUrl(settings_->homePage());
  });
  homeAction->setToolTip(tr("Home page (set it in File menu)"));
  auto* appsAction = bar->addAction(tr("Apps"), this, [this]() {
    const QString server = lastServer_.isEmpty()
                               ? QStringLiteral("ces.pubcom.org")
                               : lastServer_;
    openUrl(QStringLiteral("ces://%1/apps").arg(server));
  });
  appsAction->setToolTip(tr("Applications on this server"));
  addressBar_ = new QComboBox(this);
  addressBar_->setEditable(true);
  addressBar_->setInsertPolicy(QComboBox::NoInsert);
  addressBar_->setMinimumWidth(220);
  addressBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  addressBar_->lineEdit()->setPlaceholderText(
      tr("file:// compute:// lua:// luarpc:// ces://  (a CES address)"));
  connect(addressBar_->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
    qWarning() << "cwb-nav: trigger = lineEdit returnPressed";
    navigate();
  });
  connect(addressBar_, &QComboBox::activated, this, [this](int i) {
    qWarning() << "cwb-nav: trigger = QComboBox::activated index" << i;
    navigate();
  });
  const QString histPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/history.txt";
  QDir().mkpath(QFileInfo(histPath).path());
  urlHistory_ = std::make_unique<UrlHistory>(histPath);
  bookmarks_ = std::make_unique<UrlHistory>(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/bookmarks.txt");
  // Dropdown: bookmarks first, then history (deduped, case-insensitive).
  for (const QString& b : bookmarks_->entries()) addressBar_->addItem(b);
  for (const QString& e : urlHistory_->entries())
    if (addressBar_->findText(e, Qt::MatchFixedString) < 0)
      addressBar_->addItem(e);
  addrModel_ = new QStringListModel(this);
  addrCompleter_ = new QCompleter(addrModel_, this);
  addrCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
  addrCompleter_->setFilterMode(Qt::MatchContains);
  addrCompleter_->setCompletionMode(QCompleter::PopupCompletion);
  addressBar_->setCompleter(addrCompleter_);
  refreshAddressSuggestions();  // completer suggestions = bookmarks + history
  // Fresh install: seed the bar so Enter lands somewhere real.
  if (addressBar_->count() == 0)
    addressBar_->addItem(settings_->homePage());
  bar->addWidget(addressBar_);
  bar->addAction(tr("Go"), this, &MainWindow::navigate);

  // Loading line under the toolbar; the old document stays usable. Reaches the
  // right edge only when setContent() commits the new view.
  navigationProgress_ = new QProgressBar(this);
  navigationProgress_->setObjectName(QStringLiteral("cwbNavigationProgress"));
  navigationProgress_->setRange(0, 1000);
  navigationProgress_->setTextVisible(false);
  navigationProgress_->setFixedHeight(3);
  navigationProgress_->setStyleSheet(
      "QProgressBar{border:0;background:transparent;}"
      "QProgressBar::chunk{background:#4285f4;border:0;}");
  navigationProgress_->hide();
  navigationProgressTimer_ = new QTimer(this);
  navigationProgressTimer_->setInterval(45);
  connect(navigationProgressTimer_, &QTimer::timeout, this, [this]() {
    if (!navigationProgressActive_ || !navigationProgress_) return;
    const int value = navigationProgress_->value();
    if (value >= navigationProgressCeiling_) return;
    const int step = std::max(1, (navigationProgressCeiling_ - value) / 18);
    navigationProgress_->setValue(
        std::min(navigationProgressCeiling_, value + step));
  });
  QTimer::singleShot(0, this, [this]() { positionNavigationProgress(); });

  auto* home = new RenderPane(this);
  connect(home, &RenderPane::linkClicked, this, &MainWindow::onLinkClicked);
  connect(home, &RenderPane::hoverLink, this, &MainWindow::onHoverLink);
  connect(home, &RenderPane::findScrollTo, this, &MainWindow::scrollContentTo);
  connect(home, &RenderPane::contextMenuRequested, this,
          &MainWindow::showContextMenu);
  home->setWidgetContext(this);
  // Neutral placeholder; the launch navigation (main.cpp) opens the live home.
  home->setHtml(QStringLiteral(
      "<!doctype html><meta charset=utf-8><body style=\"margin:0;"
      "background:#ffffff\"></body>"));
  setContent(home);

  // Funding heuristic: 2-min balance poll; below the band start a hidden
  // mining session, above it stop one WE started. Manual sessions untouched.
  autoMineTimer_ = new QTimer(this);
  autoMineTimer_->setInterval(120 * 1000);
  connect(autoMineTimer_, &QTimer::timeout, this, &MainWindow::autoMineTick);
  autoMineTimer_->start();
  QTimer::singleShot(20 * 1000, this, &MainWindow::autoMineTick);

  // The miner engine outlives the optional cockpit window.
  connect(&minerEngine_, &cwb::MinerEngine::balance, this,
          [this](qint64 units, bool ok) {
            if (!ok) return;
            statusBalUnits_ = units;
            if (miningWindow_) miningWindow_->setExternalBalance(units);
            refreshStatusCorner();
            // Feed the band controller now; the 2-min poll overshoots.
            if (autoMiningSession_ && !autoMiningTarget_.isEmpty())
              onAutoMineBalance(units, units, true, autoMiningTarget_, 0);
          });
  connect(&minerEngine_, &cwb::MinerEngine::connected, this,
          [this](int) { refreshStatusCorner(); });
  connect(&minerEngine_, &cwb::MinerEngine::failed, this,
          [this](const QString& error) {
            autoMiningSession_ = false;
            autoMiningTarget_.clear();
            statusBar()->showMessage(error, 10000);
            refreshStatusCorner();
          });
  connect(&minerEngine_, &cwb::MinerEngine::stopped, this, [this]() {
    autoMiningSession_ = false;
    autoMiningTarget_.clear();
    refreshStatusCorner();
  });

  // Ctrl+R soft; Ctrl+F5 / Shift+F5 / Ctrl+Shift+R hard. F5 is the toolbar's.
  auto bindReload = [this](const QKeySequence& seq, bool hard) {
    connect(new QShortcut(seq, this), &QShortcut::activated, this,
            [this, hard]() { reload(hard); });
  };
  bindReload(QKeySequence(Qt::CTRL | Qt::Key_R), false);
  bindReload(QKeySequence(Qt::CTRL | Qt::Key_F5), true);
  bindReload(QKeySequence(Qt::SHIFT | Qt::Key_F5), true);
  bindReload(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), true);

  // Ctrl+= too: the unshifted plus key most keyboards actually have.
  auto bindZoom = [this](const QKeySequence& seq, double delta, bool reset) {
    connect(new QShortcut(seq, this), &QShortcut::activated, this,
            [this, delta, reset]() { setZoom(reset ? 1.0 : zoom_ + delta); });
  };
  bindZoom(QKeySequence::ZoomIn, 0.1, false);
  bindZoom(QKeySequence(Qt::CTRL | Qt::Key_Equal), 0.1, false);
  bindZoom(QKeySequence::ZoomOut, -0.1, false);
  bindZoom(QKeySequence(Qt::CTRL | Qt::Key_0), 0.0, true);

  // Find-in-page bar, docked at the bottom, hidden until Ctrl+F.
  findToolBar_ = new QToolBar(tr("Find"), this);
  findToolBar_->setMovable(false);
  findToolBar_->addWidget(new QLabel(tr("  Find: "), findToolBar_));
  findEdit_ = new QLineEdit(findToolBar_);
  findEdit_->setMaximumWidth(280);
  findEdit_->setClearButtonEnabled(true);
  findEdit_->setPlaceholderText(tr("find on page"));
  findToolBar_->addWidget(findEdit_);
  findCount_ = new QLabel(findToolBar_);
  findCount_->setMinimumWidth(90);
  findCount_->setStyleSheet(QStringLiteral("QLabel{color:#586069;padding:0 8px;}"));
  findToolBar_->addWidget(findCount_);
  auto findBtn = [this](const QString& glyph, const QString& tip) {
    auto* b = new QToolButton(findToolBar_);
    b->setText(glyph);
    b->setToolTip(tip);
    findToolBar_->addWidget(b);
    return b;
  };
  auto* prevBtn = findBtn(QStringLiteral("▲"), tr("Previous (Shift+F3)"));
  auto* nextBtn = findBtn(QStringLiteral("▼"), tr("Next (F3)"));
  auto* closeBtn = findBtn(QStringLiteral("✕"), tr("Close (Esc)"));
  addToolBar(Qt::BottomToolBarArea, findToolBar_);
  findToolBar_->hide();
  connect(findEdit_, &QLineEdit::textChanged, this, &MainWindow::doFind);
  connect(findEdit_, &QLineEdit::returnPressed, this, &MainWindow::findNextMatch);
  connect(prevBtn, &QToolButton::clicked, this, &MainWindow::findPrevMatch);
  connect(nextBtn, &QToolButton::clicked, this, &MainWindow::findNextMatch);
  connect(closeBtn, &QToolButton::clicked, this, &MainWindow::hideFindBar);
  connect(new QShortcut(QKeySequence::Find, this), &QShortcut::activated, this,
          &MainWindow::showFindBar);
  connect(new QShortcut(QKeySequence::FindNext, this), &QShortcut::activated,
          this, &MainWindow::findNextMatch);
  connect(new QShortcut(QKeySequence::FindPrevious, this), &QShortcut::activated,
          this, &MainWindow::findPrevMatch);
  connect(new QShortcut(QKeySequence(Qt::Key_Escape), this),
          &QShortcut::activated, this, &MainWindow::hideFindBar);
  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this),
          &QShortcut::activated, this, &MainWindow::addCurrentBookmark);

  statusBar()->showMessage(tr("Ready."));

  // Status corner: fixed widths so sections never dance. Mining toggle,
  // identity (click -> account page), balance in credit notation.
  statusMine_ = new QToolButton(this);
  statusMine_->setObjectName(QStringLiteral("miningCockpitButton"));
  statusMine_->setCheckable(true);
  statusMine_->setAutoRaise(true);
  statusMine_->setCursor(Qt::PointingHandCursor);
  statusMine_->setFixedWidth(30);
  statusMine_->setStyleSheet(QStringLiteral(
      "QToolButton{border:none;color:#777;padding:0 4px;font-size:16px;}"
      "QToolButton:hover{color:#333;}"
      "QToolButton:checked{background:#d3d8e1;color:#1a8917;"
      "border:1px inset #aab1bc;border-radius:3px;}"));
  connect(statusMine_, &QToolButton::toggled, this, [this](bool down) {
    if (down) openMining();
    else if (miningWindow_ && miningWindow_->isVisible()) miningWindow_->close();
  });
  statusId_ = new QToolButton(this);
  statusId_->setAutoRaise(true);
  statusId_->setCursor(Qt::PointingHandCursor);
  statusId_->setToolTip(tr("click to open your account page"));
  statusId_->setFixedWidth(170);
  statusId_->setStyleSheet(QStringLiteral(
      "QToolButton{border:none;color:#444;padding:0 6px;}"
      "QToolButton:hover{color:#1a8917;}"));
  connect(statusId_, &QToolButton::clicked, this, [this]() {
    const QString url = accountUrl();
    if (!url.isEmpty()) {
      openUrl(url);
      return;
    }
    const std::string hex = cwb::authorIdentity().getPublicKeyHexStr();
    QApplication::clipboard()->setText(QString::fromStdString(hex));
    statusBar()->showMessage(
        tr("public key copied — browse to a server to open your account"), 4000);
  });
  statusBal_ = new QLabel(this);
  statusBal_->setFixedWidth(110);
  statusBal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  statusBal_->setStyleSheet(QStringLiteral("color:#444;padding-right:6px;"));
  statusBal_->setToolTip(tr("your credits on the current mining server"));
  statusBar()->addPermanentWidget(statusMine_);
  statusBar()->addPermanentWidget(statusId_);
  statusBar()->addPermanentWidget(statusBal_);
  auto* cornerTimer = new QTimer(this);
  cornerTimer->setInterval(2000);
  connect(cornerTimer, &QTimer::timeout, this,
          &MainWindow::refreshStatusCorner);
  cornerTimer->start();
  refreshStatusCorner();
  // Session restore, deferred so the window shows before navigation lands.
  if (settings_->restoreSession() && !settings_->lastUrl().isEmpty()) {
    const QString last = settings_->lastUrl();
    QTimer::singleShot(0, this, [this, last]() { openUrl(last); });
  }
}

void MainWindow::setZoom(double z) {
  zoom_ = std::clamp(z, 0.5, 3.0);
  if (activeRender_) activeRender_->setZoom(zoom_);
  if (settings_) settings_->setZoomForHost(curHost_, zoom_);  // remember per site
  statusBar()->showMessage(
      tr("zoom %1%").arg(static_cast<int>(zoom_ * 100 + 0.5)));
}

void MainWindow::findInPage(const QString& query) {
  if (!findToolBar_) return;
  findToolBar_->show();
  findEdit_->setText(query);  // triggers doFind via textChanged
  if (findEdit_->text() == query) doFind(query);  // in case text was unchanged
}

void MainWindow::showFindBar() {
  if (!findToolBar_) return;
  findToolBar_->show();
  findEdit_->setFocus();
  findEdit_->selectAll();
  if (!findEdit_->text().isEmpty()) doFind(findEdit_->text());  // re-find
}

void MainWindow::hideFindBar() {
  if (!findToolBar_) return;
  findToolBar_->hide();
  if (activeRender_) activeRender_->clearFind();
  if (centralWidget()) centralWidget()->setFocus();
}

void MainWindow::doFind(const QString& query) {
  if (activeRender_) activeRender_->findText(query);
  updateFindCount();
}

void MainWindow::findNextMatch() {
  if (activeRender_) activeRender_->findNext();
  updateFindCount();
}

void MainWindow::findPrevMatch() {
  if (activeRender_) activeRender_->findPrev();
  updateFindCount();
}

void MainWindow::updateFindCount() {
  if (!findCount_) return;
  const int n = activeRender_ ? activeRender_->findMatchCount() : 0;
  const int cur = activeRender_ ? activeRender_->findCurrentIndex() : 0;
  if (findEdit_ && findEdit_->text().isEmpty())
    findCount_->clear();
  else if (n == 0)
    findCount_->setText(tr("no matches"));
  else
    findCount_->setText(tr("%1 of %2").arg(cur).arg(n));
}

void MainWindow::scrollContentTo(const QRect& docRect) {
  if (auto* sa = qobject_cast<QScrollArea*>(centralWidget()))
    sa->ensureVisible(docRect.center().x(), docRect.center().y(), 60, 140);
}

void MainWindow::refreshAddressSuggestions() {
  if (!addrModel_) return;
  QStringList list;
  if (bookmarks_) list = bookmarks_->entries();  // bookmarks first
  if (urlHistory_)
    for (const QString& h : urlHistory_->entries())
      if (!list.contains(h, Qt::CaseInsensitive)) list << h;
  addrModel_->setStringList(list);
}

void MainWindow::recordHistory(const QString& addr) {
  if (!urlHistory_) return;
  urlHistory_->add(addr);
  refreshAddressSuggestions();
}

void MainWindow::addCurrentBookmark() {
  if (!bookmarks_ || curAddr_.isEmpty()) return;
  bookmarks_->add(curAddr_);
  if (addressBar_ && addressBar_->findText(curAddr_, Qt::MatchFixedString) < 0)
    addressBar_->insertItem(0, curAddr_);  // surface it at the top of the dropdown
  refreshAddressSuggestions();
  statusBar()->showMessage(tr("bookmarked %1").arg(curAddr_));
}

void MainWindow::removeCurrentBookmark() {
  if (!bookmarks_ || curAddr_.isEmpty()) return;
  bookmarks_->remove(curAddr_);
  refreshAddressSuggestions();
  statusBar()->showMessage(tr("removed bookmark %1").arg(curAddr_));
}

void MainWindow::rebuildBookmarksMenu() {
  if (!bookmarksMenu_ || !bookmarks_) return;
  bookmarksMenu_->clear();
  bookmarksMenu_->addAction(tr("&Bookmark this page\tCtrl+D"), this,
                            &MainWindow::addCurrentBookmark);
  QAction* rm = bookmarksMenu_->addAction(tr("&Remove this page"), this,
                                          &MainWindow::removeCurrentBookmark);
  rm->setEnabled(!curAddr_.isEmpty() && bookmarks_->contains(curAddr_));
  const QStringList bm = bookmarks_->entries();
  if (!bm.isEmpty()) {
    bookmarksMenu_->addSeparator();
    for (const QString& u : bm)
      bookmarksMenu_->addAction(u, this, [this, u]() { openUrl(u); });
  }
}

void MainWindow::showContextMenu(const QString& href, const QPoint& globalPos) {
  QMenu menu(this);
  if (activeRender_) {
    QAction* copy = menu.addAction(tr("Copy"), activeRender_,
                                   &RenderPane::copySelection);
    copy->setEnabled(activeRender_->hasSelection());
    menu.addAction(tr("Select All"), activeRender_,
                   &RenderPane::selectAllText);
    menu.addSeparator();
  }
  if (!href.isEmpty()) {  // a link is under the cursor
    const LinkTarget t = resolveLink(href, curScheme_, curSelector_, curHost_,
                                     static_cast<quint16>(curPort_), curDir_);
    const QString url = t.url.isEmpty() ? href : t.url;
    if (t.kind == LinkTarget::Browser) {
      menu.addAction(tr("Open in Web Browser"), this,
                     [url]() { QDesktopServices::openUrl(QUrl(url)); });
    } else {
      menu.addAction(tr("Open"), this, [this, href]() { onLinkClicked(href); });
      menu.addAction(tr("Open in New Window"), this, [url]() {
        auto* w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        w->openUrl(url);
      });
    }
    menu.addAction(tr("Copy Link Address"), this,
                   [url]() { QApplication::clipboard()->setText(url); });
  } else {  // page area
    QAction* back = menu.addAction(tr("Back"), this, &MainWindow::goBack);
    back->setEnabled(backAction_ && backAction_->isEnabled());
    QAction* fwd = menu.addAction(tr("Forward"), this, &MainWindow::goForward);
    fwd->setEnabled(fwdAction_ && fwdAction_->isEnabled());
    menu.addAction(tr("Reload"), this, [this]() { reload(false); });
    menu.addSeparator();
    menu.addAction(tr("Copy Page Address"), this, [this]() {
      if (!curAddr_.isEmpty()) QApplication::clipboard()->setText(curAddr_);
    });
    menu.addAction(tr("Find..."), this, &MainWindow::showFindBar);
    menu.addAction(tr("Bookmark This Page"), this,
                   &MainWindow::addCurrentBookmark);
  }
  menu.exec(globalPos);
}

void MainWindow::openPreferences() {
  if (!settings_) return;
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Preferences"));
  auto* form = new QFormLayout(&dlg);

  auto* home = new QLineEdit(settings_->homePage(), &dlg);
  home->setMinimumWidth(380);
  form->addRow(tr("Home page:"), home);

  auto* zoom = new QSpinBox(&dlg);
  zoom->setRange(50, 300);
  zoom->setSingleStep(10);
  zoom->setSuffix(QStringLiteral("%"));
  zoom->setValue(static_cast<int>(settings_->zoom() * 100 + 0.5));
  form->addRow(tr("Default zoom:"), zoom);

  auto* cache = new QSpinBox(&dlg);
  cache->setRange(1, 4096);
  cache->setSuffix(tr(" MB"));
  cache->setValue(static_cast<int>(settings_->cacheMaxBytes() / (1024 * 1024)));
  form->addRow(tr("Cache size:"), cache);

  auto* restore = new QCheckBox(&dlg);
  restore->setChecked(settings_->restoreSession());
  form->addRow(tr("Restore last session:"), restore);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  if (dlg.exec() != QDialog::Accepted) return;

  settings_->setHomePage(home->text().trimmed());
  settings_->setRestoreSession(restore->isChecked());
  settings_->setZoom(zoom->value() / 100.0);  // the global default zoom
  zoom_ = settings_->zoomForHost(curHost_);    // re-apply this page's effective zoom
  if (activeRender_) activeRender_->setZoom(zoom_);
  const qint64 newCache = static_cast<qint64>(cache->value()) * 1024 * 1024;
  if (newCache != settings_->cacheMaxBytes()) {
    settings_->setCacheMaxBytes(newCache);
    // Re-open the cache at the new cap (evicts to fit); in-flight workers keep
    // their own shared_ptr to the old instance and finish safely.
    cache_ = std::make_shared<cwb::DiskCache>(cwb::DiskCache::defaultDir(),
                                              newCache);
  }
  statusBar()->showMessage(tr("preferences saved"));
}

MainWindow::~MainWindow() { resetSession(); }

ces::KeyPair MainWindow::identity() const { return cwb::loadOrCreateIdentity(); }

ces::KeyPair MainWindow::authorIdentity() const { return cwb::authorIdentity(); }

QString MainWindow::authorName() const {
  return cwb::prettyName(cwb::canonicalKeyName(cwb::preferredName()));
}

QString MainWindow::authorHandle() const {
  return cwb::canonicalKeyName(cwb::preferredName());
}

void MainWindow::wireMiningBalance() {
  if (!miningWindow_) return;
  connect(miningWindow_, &MiningWindow::balanceChanged, this,
          [this](qint64 units) {
            statusBalUnits_ = units;  // the miner's fresh sample IS your balance
            refreshStatusCorner();
          });
}

QString MainWindow::accountUrl() const {
  if (curHost_.isEmpty()) return QString();  // no server loaded
  // A luarpc:// port is a program lease with no relation to the server's
  // ports; lastServer_ remembers the parent as host[:main-port].
  if (curScheme_ == QLatin1String("luarpc")) {
    if (lastServer_.isEmpty()) return QString();
    return QStringLiteral("ces://%1/account").arg(lastServer_);
  }
  // file/lua/compute ride the rpc port; main = rpc - 1 by convention.
  quint16 mainPort;
  if (curScheme_ == QLatin1String("ces"))
    mainPort = curPort_ ? static_cast<quint16>(curPort_) : kCesMainPort;
  else {
    const quint16 rpc = curPort_ ? static_cast<quint16>(curPort_) : kCesRpcPort;
    mainPort = (rpc > 1) ? static_cast<quint16>(rpc - 1) : kCesMainPort;
  }
  return (mainPort == kCesMainPort)
             ? QStringLiteral("ces://%1/account").arg(curHost_)
             : QStringLiteral("ces://%1:%2/account").arg(curHost_).arg(mainPort);
}

void MainWindow::setContent(QWidget* w) {
  if (findToolBar_) findToolBar_->hide();  // find is per-page; close on navigation
  activeTerminal_ = qobject_cast<TerminalPane*>(w);
  activeRender_ = qobject_cast<RenderPane*>(w);
  activeFullscreen_ = nullptr;
  activeImage_ = qobject_cast<ImagePane*>(w);
  if (activeRender_ || activeImage_) {
    // Vertical scroll view; RenderPane keeps document coords so hit-testing
    // stays correct. The thin frame stops white pages bleeding into a light
    // desktop.
    auto* sa = new QScrollArea(this);
    sa->setStyleSheet(
        "QScrollArea{border:1px solid #8b929c;background:#ffffff;}");
    sa->setWidgetResizable(true);  // fill the width; scroll the height
    sa->setWidget(w);              // the scroll area takes ownership of w
    setCentralWidget(sa);          // deletes the previous central widget
  } else {
    setCentralWidget(w);           // a terminal scrolls itself
  }
  finishNavigationProgress();
}

void MainWindow::setFullscreenContent(cwb::FullscreenWidget* page) {
  auto* host = new FullscreenPageHost(page, this);
  setContent(host);
  activeFullscreen_ = page;
}

void MainWindow::loadHtml(const QString& html) {
  if (const auto params = fullscreenWidgetFromHtml(html)) {
    QWidget* raw =
        cwb::WidgetRegistry::instance().create(*params, this, this);
    if (auto* page = qobject_cast<cwb::FullscreenWidget*>(raw)) {
      setFullscreenContent(page);
      return;
    }
    delete raw;
  }
  auto* rp = new RenderPane(this);
  connect(rp, &RenderPane::linkClicked, this, &MainWindow::onLinkClicked);
  connect(rp, &RenderPane::hoverLink, this, &MainWindow::onHoverLink);
  connect(rp, &RenderPane::findScrollTo, this, &MainWindow::scrollContentTo);
  connect(rp, &RenderPane::contextMenuRequested, this,
          &MainWindow::showContextMenu);
  rp->setWidgetContext(this);  // embedded widgets act as CES clients
  if (settings_) zoom_ = settings_->zoomForHost(curHost_);  // the host's zoom
  rp->setZoom(zoom_);
  rp->setHtml(html);
  setContent(rp);
  if (!pendingAnchor_.isEmpty()) {  // #fragment: scroll once layout has settled
    const QString a = pendingAnchor_;
    pendingAnchor_.clear();
    QTimer::singleShot(0, this, [this, a]() {
      if (activeRender_) activeRender_->scrollToAnchor(a);
    });
  }
}

void MainWindow::loadError(const QString& title, const QString& url,
                           const QString& detail) {
  const QString home =
      settings_ ? settings_->homePage() : QStringLiteral("ces.pubcom.org");
  loadHtml(errorPageHtml(title, url, detail, home));
  statusBar()->showMessage(title);
}

void MainWindow::armNavDeadline(const QString& addr) {
  static constexpr int kNavDeadlineMs = 12000;  // fail fast; a real reply replaces
  navSettled_ = false;
  const int gen = navGen_;
  QTimer::singleShot(kNavDeadlineMs, this, [this, gen, addr]() {
    if (gen == navGen_ && !navSettled_) {  // still this nav, still no reply
      navSettled_ = true;
      loadError(tr("Server not responding"), addr,
                tr("No reply within %1 seconds. The server may be offline or "
                   "unreachable.")
                    .arg(kNavDeadlineMs / 1000));
    }
  });
}

void MainWindow::resetSession() {
  if (session_) {
    session_->close();
    session_.reset();
  }
  sessionMode_ = 0;
  httpAccum_.clear();
  // The current view stays until the next destination has content ready.
}

void MainWindow::navigate() {
  const QString target = addressBar_->currentText().trimmed();
  // The combobox completer commits on focus-out, firing activated() with the
  // URL already loaded; acting on it would reload and wipe an unsaved story.
  if (target == curAddr_) {
    qWarning().noquote() << "cwb-nav: navigate() IGNORED (same as current) ->"
                         << target;
    return;
  }
  qWarning().noquote() << "cwb-nav: navigate() ->" << target;
  loadAddress(target, true);
}

bool MainWindow::testAddressNavIgnoredWhenSameAsCurrent() {
  curAddr_ = QStringLiteral("file://test.invalid/same");
  if (addressBar_) addressBar_->setCurrentText(curAddr_);
  const int before = navGen_;
  navigate();  // same as current -> must early-return before loadAddress
  return navGen_ == before;  // unchanged = no navigation started (guard held)
}

void MainWindow::goBack() {
  if (histIndex_ > 0) loadAddress(history_[--histIndex_], false);
}

void MainWindow::goForward() {
  if (histIndex_ + 1 < history_.size()) loadAddress(history_[++histIndex_], false);
}

void MainWindow::reload(bool hard) {
  QString addr = curAddr_;
  if (addr.isEmpty()) addr = addressBar_->currentText().trimmed();
  if (addr.isEmpty()) return;  // nothing loaded yet
  // Soft: re-fetch the document, sub-resources may hit cache. Hard: re-fetch
  // everything. Live lanes are never cached.
  pendingPolicy_ = hard ? CachePolicy::BypassAll : CachePolicy::BypassDoc;
  statusBar()->showMessage(hard ? tr("reloading (hard)...") : tr("reloading..."));
  loadAddress(addr, false);  // re-load the current page in place; no history push
}

void MainWindow::updateNavActions() {
  if (backAction_) backAction_->setEnabled(histIndex_ > 0);
  if (fwdAction_) fwdAction_->setEnabled(histIndex_ + 1 < history_.size());
}

void MainWindow::positionNavigationProgress() {
  if (!navigationProgress_ || !addressToolBar_) return;
  const QRect barRect = addressToolBar_->geometry();
  navigationProgress_->setGeometry(0, barRect.bottom() - 1, width(), 3);
  navigationProgress_->raise();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  positionNavigationProgress();
}

void MainWindow::beginNavigationProgress() {
  if (!navigationProgress_ || !navigationProgressTimer_) return;
  navigationProgressActive_ = true;
  navigationProgressCeiling_ = 780;
  navigationProgress_->setValue(35);
  navigationProgress_->hide();
  navigationProgressTimer_->start();
  const int serial = ++navigationProgressSerial_;
  // Cache hits should feel instant, not flash a distracting blue splinter.
  QTimer::singleShot(120, this, [this, serial]() {
    if (!navigationProgressActive_ || serial != navigationProgressSerial_) return;
    positionNavigationProgress();
    navigationProgress_->show();
    navigationProgress_->raise();
  });
}

void MainWindow::advanceNavigationProgress(int permille) {
  if (!navigationProgressActive_ || !navigationProgress_) return;
  navigationProgress_->setValue(
      std::max(navigationProgress_->value(), std::min(950, permille)));
  navigationProgressCeiling_ = std::max(
      navigationProgressCeiling_, std::min(950, permille + 90));
}

void MainWindow::finishNavigationProgress() {
  if (!navigationProgressActive_ || !navigationProgress_) return;
  navigationProgressActive_ = false;
  if (navigationProgressTimer_) navigationProgressTimer_->stop();
  navigationProgress_->setValue(1000);
  const int serial = ++navigationProgressSerial_;  // cancel the show-delay shot
  if (navigationProgress_->isVisible()) {
    QTimer::singleShot(160, this, [this, serial]() {
      if (serial == navigationProgressSerial_) navigationProgress_->hide();
    });
  } else {
    navigationProgress_->hide();
  }
}

bool MainWindow::testNavigationProgress() {
  QWidget* oldContent = centralWidget();
  beginNavigationProgress();
  advanceNavigationProgress(420);
  const bool advanced = navigationProgressActive_ && navigationProgress_ &&
                        navigationProgress_->value() >= 420 &&
                        navigationProgress_->value() < 1000;
  const bool retained = centralWidget() == oldContent;
  finishNavigationProgress();
  return advanced && retained && !navigationProgressActive_ &&
         navigationProgress_->value() == 1000;
}

void MainWindow::loadAddress(const QString& addrIn, bool record) {
  if (addrIn.isEmpty()) return;
  // #fragment: navigate the base, scroll after render. Bare "#id" scrolls.
  QString addr = addrIn;
  pendingAnchor_.clear();
  if (const int h = addr.indexOf('#'); h >= 0) {
    pendingAnchor_ = addr.mid(h + 1);
    addr = addr.left(h);
    if (addr.isEmpty()) {
      if (activeRender_) activeRender_->scrollToAnchor(pendingAnchor_);
      pendingAnchor_.clear();
      return;
    }
  }
  // beforeunload: confirm before replacing a page with unsaved text. Catches
  // deliberate clicks AND stray async navigations.
  if ((activeRender_ && activeRender_->hasUnsavedContent()) ||
      (activeFullscreen_ &&
       activeFullscreen_->property("cwbUnsaved").toBool())) {
    qWarning().noquote() << "cwb-nav: leaving" << curAddr_ << "->" << addr
                         << "with UNSAVED text on the page";
    const auto btn = QMessageBox::warning(
        this, tr("Leave this page?"),
        tr("You have unsaved text on this page.\n\nLeave and discard it?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn != QMessageBox::Discard) {
      if (addressBar_ && addressBar_->currentText() != curAddr_)
        addressBar_->setCurrentText(curAddr_);
      return;  // stay put; the text (and the page) survive
    }
  }
  // A bare host is shorthand for ces://host/, never a hidden redirect.
  pendingFallback_.clear();
  {
    static const QRegularExpression bare(QStringLiteral(
        "^[A-Za-z0-9][A-Za-z0-9.-]*(:[0-9]+)?$"));
    if (!addr.contains(QLatin1String("://")) && bare.match(addr).hasMatch() &&
        addr.contains(QRegularExpression(QStringLiteral("[A-Za-z.]")))) {
      loadAddress(QStringLiteral("ces://%1/").arg(addr), record);
      return;
    }
  }
  if (addressBar_->currentText() != addr) addressBar_->setCurrentText(addr);
  if (addressBar_->findText(addr) < 0) addressBar_->insertItem(0, addr);
  if (record) {
    while (history_.size() > histIndex_ + 1) history_.removeLast();  // drop forward
    if (history_.isEmpty() || history_.last() != addr) {
      history_.append(addr);
      histIndex_ = history_.size() - 1;
    }
  }
  visited_.insert(addr);  // navigating to a url marks it visited
  updateNavActions();
  pagePolicy_ = pendingPolicy_;         // reload sets it; navigation uses cache
  pendingPolicy_ = CachePolicy::Use;    // one-shot
  setWindowTitle(addr + " - cwb");  // URL first, so it reads in taskbars/lists too
  const CesUrl u = parseCesUrl(addr);
  if (!u.valid) {
    loadError(tr("Bad address"), addr, u.error);
    return;
  }
  ++navGen_;  // invalidate any in-flight fetch: its late reply is now stale
  beginNavigationProgress();
  if (record) recordHistory(addr);  // persist the visit for autocomplete
  if (settings_) settings_->setLastUrl(addr);  // for session restore
  if (!u.host.isEmpty()) {
    // Miner target follows the last-browsed server. PoW rides the main port:
    // rpc/file ports are dropped, a non-default ces:// main port is kept.
    lastServer_ = u.host;
    if (u.isMain && u.port && u.port != kCesMainPort)
      lastServer_ += ":" + QString::number(u.port);
    if (miningWindow_) miningWindow_->setInheritedServer(lastServer_);
  }
  if (u.scheme == QLatin1String("file")) {
    navigateFile(u, addr);
    return;
  }
  if (u.scheme == QLatin1String("compute")) {
    navigateCompute(u, addr);
    return;
  }
  if (u.isMain) {  // ces:// -> server directory/resources on the main UDP port
    navigateAccount(u, addr);
    return;
  }
  const bool direct = (u.scheme == QLatin1String("luarpc"));
  if (direct && u.port == 0) {
    // Portless luarpc: each instance leases its own port, so open the public
    // instance catalog instead.
    loadAddress("file://" + u.host + "/s/instances.html", record);
    return;
  }
  if (u.scheme != QLatin1String("lua") && !direct) {
    const QString s = u.isMain ? QStringLiteral("ces") : u.scheme;
    loadError(tr("Unsupported address"), addr,
              tr("cwb has no handler for '%1://' yet.").arg(s));
    return;
  }
  statusBar()->showMessage(tr("dialing %1 over CesPlex ...").arg(addr));
  if (centralWidget()) centralWidget()->setFocus();  // browser-like: leave the URL bar
  resetSession();
  curAddr_ = addr;
  curScheme_ = u.scheme;
  curSelector_ = u.selector;
  curHost_ = u.host;
  curPath_ = u.path;
  curPort_ = u.port;
  curDir_ = u.path.left(u.path.lastIndexOf('/'));
  const std::string host = u.host.toStdString();
  const auto port = static_cast<uint16_t>(u.port);
  const quint64 pid = u.pid;

  // navGen_ guard: a slow bind/attach must not paint over the page the user
  // has moved on to.
  const int gen = navGen_;
  session_ = std::make_shared<cwb::CesLuaSession>();
  QPointer<MainWindow> self(this);
  session_->onData([self, gen](const std::string& s) {
    QString qs = QString::fromUtf8(s.data(), static_cast<int>(s.size()));
    QMetaObject::invokeMethod(
        qApp,
        [self, qs, gen]() { if (self && gen == self->navGen_) self->feedSession(qs); },
        Qt::QueuedConnection);
  });
  session_->onClose([self, gen](const std::string& r) {
    QString qr = QString::fromStdString(r);
    QMetaObject::invokeMethod(
        qApp,
        [self, qr, gen]() { if (self && gen == self->navGen_) self->sessionClosed(qr); },
        Qt::QueuedConnection);
  });

  std::shared_ptr<cwb::CesLuaSession> sess = session_;
  if (direct) {
    // luarpc: the port selects the instance; no pid, no ATTACH.
    std::thread([self, sess, host, port, gen]() {
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      const std::string err = sess->openDirect(host, port, id);
      QString qerr = QString::fromStdString(err);
      QMetaObject::invokeMethod(
          qApp,
          [self, qerr, gen]() {
            if (self && gen == self->navGen_) self->onSessionOpenedDirect(qerr);
          },
          Qt::QueuedConnection);
    }).detach();
  } else {
    std::thread([self, sess, host, port, pid, gen]() {
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      std::string hello;
      uint8_t st = 0xFF;
      const std::string err = sess->open(host, port, pid, id, hello, st);
      QString qhello =
          QString::fromUtf8(hello.data(), static_cast<int>(hello.size()));
      QString qerr = QString::fromStdString(err);
      QMetaObject::invokeMethod(
          qApp,
          [self, qhello, qerr, gen]() {
            if (self && gen == self->navGen_) self->onSessionOpened(qhello, qerr);
          },
          Qt::QueuedConnection);
    }).detach();
  }
}

void MainWindow::navigateApplicationDirectory(const QString& host,
                                              quint16 rpcPort, bool record,
                                              bool discoverRpc) {
  statusBar()->showMessage(tr("finding the live CWB service on %1 ...").arg(host));
  const int gen = navGen_;
  QPointer<MainWindow> self(this);
  std::thread([self, host, rpcPort, record, gen, discoverRpc]() {
    quint16 appPort = 0;
    quint16 actualRpc = rpcPort;
    try {
      if (discoverRpc)
        actualRpc = cwb::cesServerRpcPort(host.toStdString(), rpcPort);
      if (actualRpc == 0) throw std::runtime_error("CesPlex disabled");
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      ces::CesComputeClient cc;
      if (cc.connect(host.toStdString(), actualRpc, id) == ces::CES_OK) {
        std::vector<ces::CesComputeClient::InstanceInfo> rows;
        if (cc.instances("/s/cwb.lua", rows) == ces::CES_OK) {
          std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.pid < b.pid;
          });
          for (const auto& row : rows)
            if (row.rpcPort != 0) {
              appPort = row.rpcPort;
              break;
            }
        }
        cc.disconnect();
      }
    } catch (...) {
    }
    QMetaObject::invokeMethod(
        qApp,
        [self, host, actualRpc, appPort, record, gen]() {
          if (!self || gen != self->navGen_) return;
          if (actualRpc == 0) {
            self->loadError(
                self->tr("Applications unavailable"),
                QStringLiteral("ces://%1/apps").arg(host),
                self->tr("This CES server did not advertise a CesPlex port."));
            return;
          }
          const QString next = appPort
              ? QStringLiteral("luarpc://%1:%2/").arg(host).arg(appPort)
              : QStringLiteral("file://%1:%2/s/index.html").arg(host).arg(actualRpc);
          self->loadAddress(next, record);
        },
        Qt::QueuedConnection);
  }).detach();
}

void MainWindow::onSessionOpened(const QString& hello, const QString& err) {
  advanceNavigationProgress(330);  // transport connected / handshake answered
  if (!err.isEmpty()) {
    resetSession();
    loadError(tr("Cannot connect"), curAddr_, err);
    return;
  }
  if (hello.isEmpty()) {
    advanceNavigationProgress(510);  // application accepted the HTTP request path
    sessionMode_ = 1;  // HTTP: the program waits for the request
    if (session_) {
      const std::string get = "GET " + curPath_.toStdString() +
                              " HTTP/1.0\r\nHost: " + curHost_.toStdString() +
                              "\r\nConnection: close\r\n\r\n";
      session_->write(get);
    }
    statusBar()->showMessage(tr("http: fetching ..."));
    return;
  }
  // Program spoke first -> terminal, seeded with greeting + buffered bytes.
  QString initial = hello;
  if (!httpAccum_.isEmpty()) {
    initial += httpAccum_;
    httpAccum_.clear();
  }
  buildTerminal(initial);
}

void MainWindow::buildTerminal(const QString& initial) {
  auto* term = new TerminalPane(this);
  QPointer<MainWindow> self(this);
  connect(term, &TerminalPane::sendBytes, this, [self](const QString& b) {
    if (self && self->session_) self->session_->write(b.toStdString());
  });
  if (!initial.isEmpty()) term->appendBytes(initial);
  setContent(term);  // installs term, deletes the old view, sets activeTerminal_
  sessionMode_ = 2;
  term->setFocus();
  statusBar()->showMessage(tr("terminal: connected"));
}

void MainWindow::onSessionOpenedDirect(const QString& err) {
  advanceNavigationProgress(330);  // direct transport connected
  if (!err.isEmpty()) {
    resetSession();
    loadError(tr("Cannot connect"), curAddr_, err);
    return;
  }
  // Direct bind has no accept-greeting: probe briefly. Speaks -> terminal;
  // silent -> HTTP request.
  sessionMode_ = 3;  // pending-direct
  if (!httpAccum_.isEmpty()) {  // it spoke before we got here
    enterDirectTerminal();
    return;
  }
  QPointer<MainWindow> self(this);
  QTimer::singleShot(350, this, [self]() {
    if (self && self->sessionMode_ == 3) self->directProbeTimeout();
  });
  statusBar()->showMessage(tr("connecting (direct) ..."));
}

void MainWindow::enterDirectTerminal() {
  const QString initial = httpAccum_;
  httpAccum_.clear();
  buildTerminal(initial);
}

void MainWindow::directProbeTimeout() {
  sessionMode_ = 1;  // silent past the window -> treat as HTTP
  if (session_) {
    const std::string get = "GET " + curPath_.toStdString() +
                            " HTTP/1.0\r\nHost: " + curHost_.toStdString() +
                            "\r\nConnection: close\r\n\r\n";
    session_->write(get);
  }
  advanceNavigationProgress(510);  // protocol selected; request is on the wire
  statusBar()->showMessage(tr("http: fetching ..."));
}

void MainWindow::feedSession(const QString& bytes) {
  if (sessionMode_ == 2 && activeTerminal_) {
    activeTerminal_->appendBytes(bytes);
    return;
  }
  advanceNavigationProgress(720);  // response bytes have begun arriving
  httpAccum_ += bytes;
  if (sessionMode_ == 3) enterDirectTerminal();  // program spoke first
}

void MainWindow::sessionClosed(const QString& reason) {
  if (sessionMode_ == 1) {
    const int p = httpAccum_.indexOf(QStringLiteral("\r\n\r\n"));
    const QString body = (p < 0) ? httpAccum_ : httpAccum_.mid(p + 4);
    loadHtml(body);  // fresh RenderPane swapped in, old view destroyed
    statusBar()->showMessage(tr("rendered %1 bytes over CesPlex").arg(body.size()));
    session_.reset();
    sessionMode_ = 0;
    httpAccum_.clear();
  } else if (sessionMode_ == 2 && activeTerminal_) {
    activeTerminal_->appendBytes(QStringLiteral("\n[disconnected: %1]\n").arg(reason));
    statusBar()->showMessage(tr("terminal disconnected"));
  } else if (sessionMode_ == 3) {
    // Closed during the probe: show whatever it spoke, else report the close.
    if (!httpAccum_.isEmpty()) {
      enterDirectTerminal();
      if (activeTerminal_)
        activeTerminal_->appendBytes(
            QStringLiteral("\n[disconnected: %1]\n").arg(reason));
    } else {
      statusBar()->showMessage(tr("direct session closed"));
    }
  }
}

void MainWindow::navigateFile(const CesUrl& u, const QString& addr) {
  statusBar()->showMessage(tr("fetching %1 over CesPlex ...").arg(addr));
  if (centralWidget()) centralWidget()->setFocus();
  resetSession();  // a file read is not a session; drop any live one
  // ".../dir/" means ".../dir/index.html" (browser affordance; the store has
  // exact paths only).
  const QString fpath = normalizeFileUrlPath(u.path);
  curAddr_ = addr;
  curScheme_ = u.scheme;
  curSelector_ = u.selector;
  curHost_ = u.host;
  curPath_ = fpath;
  curPort_ = u.port;
  curDir_ = fpath.left(fpath.lastIndexOf('/'));  // page dir for relative urls
  if (settings_)  // the write widget publishes to the last file server you stood on
    settings_->setLastFileServer(u.host + ":" + QString::number(u.port));
  if (fpath == QLatin1String("/")) {
    // The store root is not a readable name; render our own front door.
    loadHtml(fileZonesPageHtml(u.host, u.port));
    statusBar()->showMessage(tr("file zones of %1").arg(u.host));
    return;
  }
  if (fileTypeFor(fpath).kind == FileKind::Download) {
    // Large/binary files stream to disk (video, archives, pdf); never buffered.
    startDownload(u.host, static_cast<quint16>(u.port), fpath, addr);
    return;
  }
  const std::string host = u.host.toStdString();
  const auto port = static_cast<uint16_t>(u.port);
  const std::string path = fpath.toStdString();
  const QString qpath = fpath;
  // Canonical cache key, shared with the sub-resource loader's key shape.
  const QString key =
      "file://" + u.host + ":" + QString::number(u.port) + fpath;
  const bool tryCache = (pagePolicy_ == CachePolicy::Use);
  auto cache = cache_;
  armNavDeadline(addr);  // an unreachable rpc port can hang; fail fast
  const int gen = navGen_;  // drop this reply if a newer navigation supersedes it
  QPointer<MainWindow> self(this);
  std::thread([self, host, port, path, qpath, key, tryCache, cache, gen]() {
    auto deliver = [&](const QByteArray& qb, uint8_t code, bool fromCache) {
      QMetaObject::invokeMethod(
          qApp,
          [self, qb, code, qpath, fromCache, gen]() {
            if (self && gen == self->navGen_)
              self->onFileFetched(qb, static_cast<int>(code), qpath, fromCache);
          },
          Qt::QueuedConnection);
    };
    try {
      if (tryCache) {
        if (auto hit = cache->get(key)) {
          deliver(*hit, ces::CES_OK, true);
          return;
        }
      }
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      std::string bytes;
      const uint8_t code = cwb::cesFileFetch(host, port, id, path, bytes);
      QByteArray qb(bytes.data(), static_cast<int>(bytes.size()));
      if (code == ces::CES_OK) cache->put(key, qb);
      deliver(qb, code, false);
    } catch (...) {
      deliver(QByteArray(), cwb::CWB_FETCH_INTERNAL, false);
    }
  }).detach();
}

void MainWindow::onFileFetched(const QByteArray& bytes, int code,
                               const QString& path, bool fromCache) {
  advanceNavigationProgress(760);  // complete document reply is available
  navSettled_ = true;  // a reply arrived; the fail-fast deadline is moot
  if (code == ces::CES_ERROR_FILE_NOT_FOUND && !pendingFallback_.isEmpty()) {
    // Bare-host landing without the cwb extension: fall back to the catalog.
    const QString fb = pendingFallback_;
    pendingFallback_.clear();
    loadAddress(fb, false);
    return;
  }
  if (code == ces::CES_ERROR_ORIGIN_NOT_FOUND) {
    // Reads are paid; no account here yet. Send the user to their live
    // account view (shows balance, auto-mines them in), not a static error.
    const QString acct = accountUrl();
    if (!acct.isEmpty()) {
      statusBar()->showMessage(
          tr("no account on %1 yet -- mine here, then reopen the page")
              .arg(curHost_));
      loadAddress(acct, false);
      return;
    }
    loadError(tr("No account yet"), curAddr_,
              tr("Reads are paid; mine some credits first, then reload."));
    return;
  }
  if (code == cwb::CWB_FETCH_TOO_LARGE) {
    // Too big for RAM -> streamed save-to-disk.
    statusBar()->showMessage(
        tr("%1 is too large to display -- offering download").arg(curAddr_));
    finishNavigationProgress();
    startDownload(curHost_, static_cast<quint16>(curPort_), curPath_, curAddr_);
    return;
  }
  if (code == cwb::CWB_FETCH_INTERNAL) {
    loadError(tr("Cannot read file"), curAddr_,
              tr("the transfer failed unexpectedly -- try reloading (F5)"));
    return;
  }
  if (code != ces::CES_OK) {
    const QString title = (code == ces::CES_ERROR_FILE_NOT_FOUND)
                              ? tr("Not found")
                              : tr("Cannot read file");
    QString detail =
        QString::fromUtf8(ces::errorString(static_cast<uint8_t>(code)));
    if (code == ces::CES_ERROR_BAD_NAME)
      detail += tr(" -- file paths must start with /s/, /p/, /h/<pubkey>/ or "
                   "/f/<name>/ (open file://%1:%2/ for the zone guide)")
                    .arg(curHost_)
                    .arg(curPort_);
    loadError(title, curAddr_, detail);
    return;
  }
  const QString via =
      fromCache ? tr("from cache") : tr("over CesPlex");
  const FileType ft = fileTypeFor(path);
  if (ft.kind == FileKind::Html) {
    const QString htmlStr = QString::fromUtf8(bytes);
    loadHtml(htmlStr);
    statusBar()->showMessage(
        tr("rendered %1 bytes %2").arg(bytes.size()).arg(via));
    markVisitedLinks(htmlStr);  // color links to already-visited pages
    startSubresourceLoad();     // fetch CSS/images, relayout as each lands
  } else if (ft.kind == FileKind::Markdown) {
    const QString htmlStr = mdToStyledHtml(bytes);
    loadHtml(htmlStr);
    statusBar()->showMessage(
        tr("rendered markdown (%1 bytes) %2").arg(bytes.size()).arg(via));
    markVisitedLinks(htmlStr);  // color links to already-visited pages
    startSubresourceLoad();     // load any images the doc references
  } else if (ft.kind == FileKind::Text) {
    QString body = QString::fromUtf8(bytes);
    body.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    loadHtml(QStringLiteral(
                 "<!doctype html><html><body style='margin:16px;"
                 "background:#ffffff'><pre style='font-family:monospace;"
                 "white-space:pre-wrap;word-break:break-word;color:#1a1d22'>"
                 "%1</pre></body></html>")
                 .arg(body));
    statusBar()->showMessage(
        tr("rendered %1 bytes %2").arg(bytes.size()).arg(via));
  } else if (ft.kind == FileKind::Code) {
    loadHtml(cwb::codePageHtml(QString::fromUtf8(bytes), path.section('.', -1)));
    statusBar()->showMessage(
        tr("rendered %1 bytes %2").arg(bytes.size()).arg(via));
  } else if (ft.kind == FileKind::Csv) {
    const QChar sep =
        path.section('.', -1).toLower() == QLatin1String("tsv") ? QLatin1Char('\t')
                                                                : QLatin1Char(',');
    loadHtml(cwb::csvToHtmlPage(QString::fromUtf8(bytes), sep));
    statusBar()->showMessage(
        tr("rendered %1 bytes %2").arg(bytes.size()).arg(via));
  } else if (ft.kind == FileKind::Json) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    QString note;
    QString body;
    if (perr.error == QJsonParseError::NoError && !doc.isNull()) {
      body = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    } else {
      body = QString::fromUtf8(bytes);  // invalid: show raw, never lose content
      note = tr(" (invalid JSON, shown raw)");
    }
    body.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    loadHtml(QStringLiteral(
                 "<!doctype html><html><body style='margin:16px;"
                 "background:#ffffff'><pre style='font-family:monospace;"
                 "white-space:pre-wrap;word-break:break-word;color:#1a1d22'>"
                 "%1</pre></body></html>")
                 .arg(body));
    statusBar()->showMessage(
        tr("rendered json (%1 bytes)%2 %3").arg(bytes.size()).arg(note).arg(via));
  } else if (ft.kind == FileKind::Media) {
    const bool audioOnly = ft.contentType.startsWith(QLatin1String("audio/"));
    auto* mp = new MediaPane(bytes, audioOnly, this);
    connect(mp, &MediaPane::downloadRequested, this,
            [this, path, bytes]() { saveDownload(path, bytes); });
    setContent(mp);
    statusBar()->showMessage(
        tr("playing %1 (%2 bytes) %3")
            .arg(path.section('/', -1))
            .arg(bytes.size())
            .arg(via));
  } else if (ft.kind == FileKind::Image) {
    const bool isSvg = ft.contentType.startsWith(QLatin1String("image/svg"));
    auto* ip = new ImagePane(this);
    if (ip->load(bytes, isSvg)) {
      setContent(ip);  // deletes the old view, sets activeImage_
      const QSize sz = ip->imageSize();
      statusBar()->showMessage(tr("image %1x%2, %3 bytes %4")
                                   .arg(sz.width())
                                   .arg(sz.height())
                                   .arg(bytes.size())
                                   .arg(via));
    } else {
      delete ip;  // undecodable (missing plugin/corrupt) -> offer to save
      saveDownload(path, bytes);
    }
  } else {
    saveDownload(path, bytes);  // keeps the current page, pops a save dialog
  }
}

void MainWindow::navigateCompute(const CesUrl& u, const QString& addr) {
  // A path past the .lua source is a dynamic-app address: resolve the live
  // instance and relay into it. Pids die with restarts, the source does not:
  // compute://host/s/vellum.lua/by/ada -> lua://<live-pid>@host/by/ada.
  if (const int mark = u.path.indexOf(QLatin1String(".lua/")); mark > 0) {
    const QString source = u.path.left(mark + 4);
    const QString rest = u.path.mid(mark + 4);
    statusBar()->showMessage(
        tr("finding the live %1 on %2 ...").arg(source, u.host));
    const std::string host = u.host.toStdString();
    const auto port = static_cast<uint16_t>(u.port);
    const std::string src = source.toStdString();
    const int gen = navGen_;
    QPointer<MainWindow> self(this);
    std::thread([self, host, port, src, source, rest, gen, u, addr]() {
      quint64 pid = 0;
      try {
        ces::KeyPair id = cwb::loadOrCreateIdentity();
        ces::CesComputeClient cc;
        if (cc.connect(host, port, id) == ces::CES_OK) {
          std::vector<ces::CesComputeClient::InstanceInfo> rows;
          if (cc.instances(src, rows) == ces::CES_OK && !rows.empty()) {
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) { return a.pid < b.pid; });
            pid = rows.front().pid;
          }
          cc.disconnect();
        }
      } catch (...) {
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, pid, source, rest, gen, u, addr]() {
            if (!self || gen != self->navGen_) return;
            if (!pid) {
              self->loadError(
                  self->tr("Application not running"), addr,
                  self->tr("No live instance of %1 on this server.")
                      .arg(source));
              return;
            }
            // History keeps the stable compute:// address; back/forward
            // re-resolve.
            self->loadAddress(QStringLiteral("lua://%1@%2:%3%4")
                                  .arg(pid)
                                  .arg(u.host)
                                  .arg(u.port)
                                  .arg(rest),
                              false);
          },
          Qt::QueuedConnection);
    }).detach();
    return;
  }
  statusBar()->showMessage(tr("querying %1 over CesPlex ...").arg(addr));
  if (centralWidget()) centralWidget()->setFocus();
  resetSession();  // an instance query is not a session; drop any live one
  curAddr_ = addr;
  curScheme_ = u.scheme;
  curSelector_ = u.selector;
  curHost_ = u.host;
  curPath_ = u.path;
  curPort_ = u.port;
  curDir_ = u.path.left(u.path.lastIndexOf('/'));
  armNavDeadline(addr);  // an unreachable rpc port can hang; fail fast
  const std::string host = u.host.toStdString();
  const auto port = static_cast<uint16_t>(u.port);
  const std::string path = u.path.toStdString();
  const int gen = navGen_;  // drop this reply if a newer navigation supersedes it
  QPointer<MainWindow> self(this);
  std::thread([self, host, port, path, gen]() {
    try {
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      std::string html;
      const uint8_t code = cwb::cesComputeFetch(host, port, id, path, html);
      QString qhtml =
          QString::fromUtf8(html.data(), static_cast<int>(html.size()));
      QMetaObject::invokeMethod(
          qApp,
          [self, qhtml, code, gen]() {
            if (self && gen == self->navGen_)
              self->onComputeFetched(qhtml, static_cast<int>(code));
          },
          Qt::QueuedConnection);
    } catch (...) {
      QMetaObject::invokeMethod(
          qApp,
          [self, gen]() {
            if (self && gen == self->navGen_)
              self->onComputeFetched(QString(), ces::CES_ERROR_INTERNAL);
          },
          Qt::QueuedConnection);
    }
  }).detach();
}

void MainWindow::onComputeFetched(const QString& html, int code) {
  advanceNavigationProgress(760);  // query reply is ready to render
  navSettled_ = true;  // a reply arrived; the fail-fast deadline is moot
  if (code != ces::CES_OK) {
    loadError(tr("Cannot list instances"), curAddr_,
              QString::fromUtf8(ces::errorString(static_cast<uint8_t>(code))));
    return;
  }
  loadHtml(html);  // generated directory; its instance links navigate in cwb
  markVisitedLinks(html);
  statusBar()->showMessage(tr("compute instances over CesPlex"));
}

void MainWindow::navigateAccount(const CesUrl& u, const QString& addr) {
  if (u.path == QLatin1String("/apps")) {
    // Discover the advertised CesPlex port, then dial the live directory.
    navigateApplicationDirectory(u.host, u.port, false, true);
    return;
  }
  const bool serverRoot = u.path == QLatin1String("/");
  const bool identityView = u.path == QLatin1String("/identity");
  const bool infoView = u.path == QLatin1String("/info");
  statusBar()->showMessage(
      serverRoot ? tr("reading server capabilities from %1 ...").arg(addr)
                 : tr("querying %1 on the main port ...").arg(addr));
  if (centralWidget()) centralWidget()->setFocus();
  resetSession();  // a query is not a session; drop any live one
  curAddr_ = addr;
  curScheme_ = u.scheme;
  curSelector_ = u.selector;
  curHost_ = u.host;
  curPath_ = u.path;
  curPort_ = u.port;
  curDir_ = u.path.left(u.path.lastIndexOf('/'));
  armNavDeadline(addr);  // this query can hang on an unreachable host; fail fast
  QString pk;
  if (!serverRoot) {
    // /account = this browser; /account/<pubkey> = another; bare /<pubkey>
    // kept as a compatibility path.
    if (u.path == QLatin1String("/account") || identityView || infoView)
      pk.clear();
    else if (u.path.startsWith(QLatin1String("/account/")))
      pk = u.path.mid(QStringLiteral("/account/").size());
    else
      pk = u.path.mid(1);
  }
  const std::string host = u.host.toStdString();
  const auto port = static_cast<uint16_t>(u.port);
  const std::string pubkey =
      serverRoot ? std::string("__CES_SERVER_ROOT__")
                 : infoView ? std::string("__CES_SERVER_INFO__")
                            : pk.toStdString();
  const cwb::AccountView accountView = identityView
                                           ? cwb::AccountView::Identity
                                           : cwb::AccountView::Overview;
  const int gen = navGen_;  // drop this reply if a newer navigation supersedes it
  QPointer<MainWindow> self(this);
  std::thread([self, host, port, pubkey, accountView, gen]() {
    try {
      std::string html;
      const uint8_t code =
          pubkey == "__CES_SERVER_ROOT__"
              ? cwb::cesServerDirectoryFetch(host, port, html)
          : pubkey == "__CES_SERVER_INFO__"
              ? cwb::cesServerInfoFetch(host, port, html)
              : cwb::cesAccountFetch(host, port, pubkey, html, accountView);
      QString qhtml =
          QString::fromUtf8(html.data(), static_cast<int>(html.size()));
      QMetaObject::invokeMethod(
          qApp,
          [self, qhtml, code, gen]() {
            if (self && gen == self->navGen_)
              self->onAccountFetched(qhtml, static_cast<int>(code));
          },
          Qt::QueuedConnection);
    } catch (...) {
      QMetaObject::invokeMethod(
          qApp,
          [self, gen]() {
            if (self && gen == self->navGen_)
              self->onAccountFetched(QString(), ces::CES_ERROR_INTERNAL);
          },
          Qt::QueuedConnection);
    }
  }).detach();
}

void MainWindow::onAccountFetched(const QString& html, int code) {
  advanceNavigationProgress(760);  // account reply is ready to render
  navSettled_ = true;  // a reply arrived; the fail-fast deadline is moot
  if (code != ces::CES_OK) {
    loadError(curPath_ == QLatin1String("/") ? tr("Cannot query server")
                                              : tr("Cannot query account"),
              curAddr_,
              QString::fromUtf8(ces::errorString(static_cast<uint8_t>(code))));
    return;
  }
  loadHtml(html);
  statusBar()->showMessage(curPath_ == QLatin1String("/")
                               ? tr("server capabilities on the main port")
                               : tr("account queried on the main port"));
}

void MainWindow::saveDownload(const QString& path, const QByteArray& bytes) {
  QString suggested = path.section('/', -1);
  if (suggested.isEmpty()) suggested = QStringLiteral("download.bin");
  const QString dest =
      QFileDialog::getSaveFileName(this, tr("Save file"), suggested);
  if (dest.isEmpty()) {
    statusBar()->showMessage(tr("download cancelled"));
    return;
  }
  QFile f(dest);
  if (!f.open(QIODevice::WriteOnly)) {
    statusBar()->showMessage(tr("cannot write %1").arg(dest));
    return;
  }
  f.write(bytes);
  f.close();
  statusBar()->showMessage(
      tr("saved %1 bytes to %2").arg(bytes.size()).arg(dest));
}

void MainWindow::startDownload(const QString& host, quint16 port,
                               const QString& path, const QString& addr) {
  QString suggested = path.section('/', -1);
  if (suggested.isEmpty()) suggested = QStringLiteral("download.bin");
  const QString dest =
      QFileDialog::getSaveFileName(this, tr("Save file"), suggested);
  if (dest.isEmpty()) {
    statusBar()->showMessage(tr("download cancelled"));
    return;
  }
  statusBar()->showMessage(tr("downloading %1 ...").arg(addr));
  QPointer<MainWindow> self(this);
  const std::string h = host.toStdString();
  const std::string p = path.toStdString();
  std::thread([self, h, port, p, dest]() {
    auto post = [self](const QString& msg) {
      QMetaObject::invokeMethod(
          qApp,
          [self, msg]() {
            if (self) self->statusBar()->showMessage(msg);
          },
          Qt::QueuedConnection);
    };
    try {
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      QFile out(dest);
      if (!out.open(QIODevice::WriteOnly)) {
        post(tr("cannot write %1").arg(dest));
        return;
      }
      auto sink = [&out](const uint8_t* d, size_t n) -> bool {
        return out.write(reinterpret_cast<const char*>(d),
                         static_cast<qint64>(n)) == static_cast<qint64>(n);
      };
      qint64 lastPct = -1;
      auto prog = [&](uint64_t done, uint64_t total) {
        const qint64 pct = total ? static_cast<qint64>(done * 100 / total) : 0;
        if (pct != lastPct) {
          lastPct = pct;
          post(tr("downloading %1%  (%2 / %3 bytes)").arg(pct).arg(done).arg(total));
        }
      };
      const uint8_t rc = cwb::cesFileDownload(h, port, id, p, sink, prog);
      out.close();
      if (rc == ces::CES_OK) {
        post(tr("saved to %1").arg(dest));
      } else {
        out.remove();  // drop the partial file
        const QString why = (rc == cwb::CWB_FETCH_INTERNAL)
                                ? tr("transfer or disk write failed")
                                : QString::fromUtf8(ces::errorString(rc));
        post(tr("download failed: %1").arg(why));
      }
    } catch (...) {
      post(tr("download failed"));
    }
  }).detach();
}

void MainWindow::startSubresourceLoad() {
  resAttempted_.clear();
  loadNextResource();
}

void MainWindow::markVisitedLinks(const QString& html) {
  if (!activeRender_) return;
  static const QRegularExpression re(
      QStringLiteral("href\\s*=\\s*[\"']([^\"']*)[\"']"),
      QRegularExpression::CaseInsensitiveOption);
  QStringList visitedRaw;
  QSet<QString> seen;
  auto it = re.globalMatch(html);
  while (it.hasNext()) {
    const QString raw = it.next().captured(1);
    if (raw.isEmpty() || seen.contains(raw)) continue;
    seen.insert(raw);
    const LinkTarget t = resolveLink(raw, curScheme_, curSelector_, curHost_,
                                     static_cast<quint16>(curPort_), curDir_);
    if (t.kind == LinkTarget::Navigate && visited_.contains(t.url))
      visitedRaw << raw;
  }
  if (!visitedRaw.isEmpty()) activeRender_->setVisitedHrefs(visitedRaw);
}

QString MainWindow::resolveResourcePath(const QString& url) const {
  // Same-server file resources only.
  if (url.contains(QLatin1String("://")) ||
      url.startsWith(QLatin1String("data:")) ||
      url.startsWith(QLatin1String("//")))
    return QString();
  const QString path = url.startsWith('/') ? url : (curDir_ + "/" + url);
  return QDir::cleanPath(path);
}

void MainWindow::loadNextResource() {
  RenderPane* rp = activeRender_;
  if (!rp) return;  // navigated away
  QString next;
  bool isCss = false;
  for (const QString& u : rp->wantedCss())
    if (!resAttempted_.contains(u)) {
      next = u;
      isCss = true;
      break;
    }
  if (next.isEmpty())
    for (const QString& u : rp->wantedImages())
      if (!resAttempted_.contains(u)) {
        next = u;
        break;
      }
  if (next.isEmpty()) {  // nothing left -> fully loaded
    statusBar()->showMessage(tr("page loaded"));
    return;
  }
  resAttempted_.insert(next);
  const QString path = resolveResourcePath(next);
  if (path.isEmpty()) {  // external/unfetchable -> skip, continue
    loadNextResource();
    return;
  }
  const std::string host = curHost_.toStdString();
  const auto port = static_cast<uint16_t>(curPort_);
  const std::string cpath = path.toStdString();
  const QString key =  // same key shape as the document fetch
      "file://" + curHost_ + ":" + QString::number(curPort_) + path;
  const bool tryCache = (pagePolicy_ != CachePolicy::BypassAll);
  auto cache = cache_;
  const int gen = navGen_;  // a late reply must not feed the NEXT page
  QPointer<MainWindow> self(this);
  std::thread([self, host, port, cpath, next, isCss, key, tryCache, cache,
               gen]() {
    auto deliver = [&](const QByteArray& qb, uint8_t code) {
      QMetaObject::invokeMethod(
          qApp,
          [self, next, isCss, qb, code, gen]() {
            if (self && gen == self->navGen_)
              self->onResourceFetched(next, isCss, qb, static_cast<int>(code));
          },
          Qt::QueuedConnection);
    };
    try {
      if (tryCache) {
        if (auto hit = cache->get(key)) {
          deliver(*hit, ces::CES_OK);
          return;
        }
      }
      ces::KeyPair id = cwb::loadOrCreateIdentity();
      std::string bytes;
      const uint8_t code = cwb::cesFileFetch(host, port, id, cpath, bytes);
      QByteArray qb(bytes.data(), static_cast<int>(bytes.size()));
      if (code == ces::CES_OK) cache->put(key, qb);
      deliver(qb, code);
    } catch (...) {
      deliver(QByteArray(), cwb::CWB_FETCH_INTERNAL);
    }
  }).detach();
}

void MainWindow::onResourceFetched(const QString& url, bool isCss,
                                   const QByteArray& bytes, int code) {
  RenderPane* rp = activeRender_;
  if (!rp) return;  // navigated away mid-load
  if (code == ces::CES_OK) {
    if (isCss) {
      rp->provideCss(url, QString::fromUtf8(bytes));
    } else {
      QImage img;
      img.loadFromData(bytes);  // raster; SVG needs the qsvg image plugin
      if (!img.isNull()) rp->provideImage(url, img);
    }
  }
  loadNextResource();  // serial: one resource at a time
}

void MainWindow::openUrl(const QString& url) {
  loadAddress(url, true);
}

void MainWindow::onLinkClicked(const QString& href) {
  if (href.startsWith('#')) {  // same-page fragment: scroll, don't navigate
    if (activeRender_) activeRender_->scrollToAnchor(href.mid(1));
    return;
  }
  const LinkTarget t = resolveLink(href, curScheme_, curSelector_, curHost_,
                                   static_cast<quint16>(curPort_), curDir_);
  if (t.kind == LinkTarget::Browser)
    QDesktopServices::openUrl(QUrl(t.url));  // the web -> the OS browser
  else if (t.kind == LinkTarget::Navigate)
    openUrl(t.url);  // relative or full CES address -> navigate here
}

void MainWindow::onHoverLink(const QString& href) {
  if (href.isEmpty()) {
    statusBar()->clearMessage();
    return;
  }
  if (href.startsWith('#')) {  // same-page anchor
    statusBar()->showMessage(href);
    return;
  }
  const LinkTarget t = resolveLink(href, curScheme_, curSelector_, curHost_,
                                   static_cast<quint16>(curPort_), curDir_);
  statusBar()->showMessage(t.url.isEmpty() ? href : t.url);  // where it goes
}

void MainWindow::openMining() {
  if (!miningWindow_) {
    miningWindow_ = new MiningWindow(&minerEngine_, this);
    miningWindow_->setWindowFlag(Qt::Window);
    miningWindow_->setInheritedServer(lastServer_);
    wireMiningBalance();
    connect(miningWindow_, &MiningWindow::cockpitClosed, this, [this]() {
      statusMine_->setChecked(false);
    });
  }
  statusMine_->setChecked(true);
  miningWindow_->show();
  miningWindow_->raise();
  miningWindow_->activateWindow();
}

void MainWindow::openMiningAndStart(const QString& server) {
  openMining();
  miningWindow_->startFromCli(server);
}

void MainWindow::refreshStatusCorner() {
  // Local state only, never the network. Pickaxe = hashing; hollow circle =
  // idle; button depression = cockpit open.
  const bool mining = minerEngine_.active();
  statusMine_->setText(mining ? QString(QChar(0x26CF)) : QString(QChar(0x25CB)));
  statusMine_->setToolTip(mining ? tr("mining — open cockpit")
                                 : tr("not mining — open cockpit"));
  QString id = cwb::prettyName(cwb::canonicalKeyName(cwb::preferredName()));
  if (id.isEmpty()) {
    const std::string hex = cwb::authorIdentity().getPublicKeyHexStr();
    id = QString::fromStdString(hex.substr(0, 8));
  }
  statusId_->setText(
      statusId_->fontMetrics().elidedText(id, Qt::ElideRight, 150));
  statusBal_->setText(statusBalUnits_ < 0
                          ? QString(QChar(0x2014))  // em dash: unknown yet
                          : cwb::creditsText(statusBalUnits_));
}

void MainWindow::autoMineTick() {
  // Same slow tick also mirrors the worktree onto the current file server
  // (feed + restore). Best-effort, own thread.
  if (autoMineAllowed_ && settings_) {
    const QString fs = settings_->lastFileServer();
    if (fs.contains(':') && !cwb::preferredName().isEmpty()) {
      const QString host = fs.section(':', 0, 0);
      const quint16 rpc = fs.section(':', 1, 1).toUShort();
      QPointer<MainWindow> self(this);
      std::thread([self, host, rpc]() {
        try {
          const QString msg = cwb::syncWorksToServer(host, rpc);
          if (!msg.isEmpty())
            QMetaObject::invokeMethod(
                qApp,
                [self, msg]() {
                  if (self) self->statusBar()->showMessage(msg, 8000);
                },
                Qt::QueuedConnection);
        } catch (...) {
        }
      }).detach();
    }
  }
  if (!autoMineAllowed_ || !settings_) return;
  const QString target = lastServer_.isEmpty()
                             ? QStringLiteral("ces.pubcom.org")
                             : lastServer_;
  const QString host = target.section(':', 0, 0);
  const quint16 port = target.contains(':')
                           ? target.section(':', 1, 1).toUShort()
                           : kCesMainPort;
  QPointer<MainWindow> self(this);
  std::thread([self, host, port, target]() {
    // Also nudge our preferred name onto this server as we settle here.
    cwb::ensureNameOnServer(host, port);
    const qint64 bal = cwb::queryOwnBalance(host, port);
    const bool ok = bal >= 0;
    const qint64 amount = ok ? bal : 0;
    QMetaObject::invokeMethod(
        qApp,
        [self, amount, ok, target]() {
          if (self) self->onAutoMineBalance(amount, amount, ok, target, 0);
        },
        Qt::QueuedConnection);
  }).detach();
}

void MainWindow::onAutoMineBalance(qint64 sumUnits, qint64 bandUnits, bool ok,
                                   const QString& target, qint64 sweptUnits) {
  if (ok) {
    statusBalUnits_ = sumUnits;
    refreshStatusCorner();
    if (miningWindow_) miningWindow_->setExternalBalance(sumUnits);
  }
  if (sweptUnits > 0)
    statusBar()->showMessage(
        tr("moved %1 credits from mining into your account")
            .arg(cwb::creditsText(sweptUnits)),
        6000);
  if (!ok || !settings_ || !settings_->autoMineEnabled()) return;
  const qint64 lo = qint64(settings_->autoMineMin()) * qint64(cwb::kCreditUnit);
  const qint64 hi = qint64(settings_->autoMineMax()) * qint64(cwb::kCreditUnit);
  if (hi <= 0) return;  // a zeroed band means "never"
  if (bandUnits < lo) {
    if (!minerEngine_.active()) {
      const int hw = std::max(1, int(std::thread::hardware_concurrency()));
      autoMiningSession_ = true;
      autoMiningTarget_ = target;
      minerEngine_.start(target, cwb::loadOrCreateIdentity(),
                         std::max(1, hw / 2), 0, 0);
      statusBar()->showMessage(
          tr("auto-mining: balance below %1 credits on %2")
              .arg(cwb::creditsText(lo))
              .arg(target),
          8000);
    }
  } else if (bandUnits >= hi && autoMiningSession_ && minerEngine_.active()) {
    minerEngine_.stop();
    statusBar()->showMessage(
        tr("auto-mining done: balance above %1 credits")
            .arg(cwb::creditsText(hi)),
        8000);
  }
}

void MainWindow::newWindow() {
  auto* w = new MainWindow();
  w->setAttribute(Qt::WA_DeleteOnClose);
  w->show();
}

void MainWindow::about() {
  QMessageBox::about(
      this, tr("About cwb"),
      tr("CWB, the CES Web Browser.\n\n"
         "Speaks CesPlex directly, mines its own credits, renders CES pages."));
}
