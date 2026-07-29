#include "mainwindow.h"
#include <QToolButton>
#include "miningwindow.h"
#include "recorder.h"
#include "renderpane.h"
#include "cesurl.h"
#include "cesdial.h"
#include "cesidentity.h"
#include "settings.h"
#include "identityreg.h"
#include "names.h"

#include <ces/types.h>

#include <CLI/CLI.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTextBlock>
#include <QSvgRenderer>
#include <QPointer>
#include <QStandardPaths>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Headless render harness: render HTML (or the built-in sample) with litehtml
// and write a PNG, no window shown.
static int runShot(const QString& out, const QString& htmlFile, int width,
                   double zoom) {
  QString html;
  if (!htmlFile.isEmpty()) {
    QFile f(htmlFile);
    if (f.open(QIODevice::ReadOnly)) html = QString::fromUtf8(f.readAll());
  }
  if (html.isEmpty()) html = cwbSampleDashboardHtml();
  RenderPane pane;
  pane.setZoom(zoom);
  pane.setHtml(html);
  const QImage img = pane.renderToImage(width > 0 ? width : 900);
  const bool ok = !img.isNull() && img.save(out, "PNG");
  std::fprintf(stderr, ok ? "shot ok: %s (%dx%d)\n" : "shot FAILED: %s\n",
               out.toUtf8().constData(), img.width(), img.height());
  return ok ? 0 : 1;
}

// Headless whole-window capture: the full browser chrome plus rendered page.
static int runWindowShot(const QString& out, const QString& openUrl,
                         const QString& htmlFile, const QString& find,
                         const QString& clickButton, int clickWaitMs,
                         int clickCount, bool holdButton,
                         const QString& writerTitle,
                         const QString& writerType, int writerBodyLines,
                         int scrollY, int windowWidth, int windowHeight) {
  QString html;
  if (!htmlFile.isEmpty()) {
    QFile f(htmlFile);
    if (f.open(QIODevice::ReadOnly)) html = QString::fromUtf8(f.readAll());
  }
  auto* w = new MainWindow();
  w->disableAutoMine();  // a capture harness must never start real mining
  w->resize(windowWidth, windowHeight);
  if (!html.isEmpty()) w->loadHtml(html);
  w->show();
  int delayMs = 250;
  if (!openUrl.isEmpty()) {
    w->openUrl(openUrl);
    delayMs = 5000;  // let the CesPlex fetch land before capturing
  }
  const auto grab = [w, out]() {
    const QPixmap pm = w->grab();
    const bool ok = pm.save(out, "PNG");
    std::fprintf(stderr, ok ? "windowshot ok: %s (%dx%d)\n" : "windowshot FAILED\n",
                 out.toUtf8().constData(), pm.width(), pm.height());
    qApp->quit();
  };
  QTimer::singleShot(delayMs, [w, find, clickButton, clickWaitMs, clickCount,
                               holdButton,
                               writerTitle, writerType, writerBodyLines,
                               scrollY, grab]() {
    auto* writer =
        w->findChild<QTextEdit*>(QStringLiteral("cwbWriteEditor"));
    if (!writerTitle.isEmpty() && writer) {
      QTextCursor c(writer->document()->firstBlock());
      c.select(QTextCursor::BlockUnderCursor);
      c.insertText(writerTitle);
      c.movePosition(QTextCursor::EndOfBlock);
      c.insertBlock();  // real writer flow: title, Enter, then body
      writer->setTextCursor(c);
    }
    if (!writerType.isEmpty())
      if (writer) {
        writer->setFocus();
        QTextCursor end = writer->textCursor();
        end.movePosition(QTextCursor::End);
        writer->setTextCursor(end);
        auto* timer = new QTimer(w);
        auto* at = new int(0);
        QObject::connect(timer, &QTimer::timeout,
                         [w, writer, timer, at, writerType, grab] {
          writer->insertPlainText(writerType.mid(*at, 1));
          ++*at;
          auto* outer = qobject_cast<QScrollArea*>(w->centralWidget());
          std::fprintf(stderr,
                       "writertype %d: editorY=%d editorH=%d "
                       "outer=%d/%d inner=%d/%d\n",
                       *at, writer->mapTo(w, QPoint()).y(), writer->height(),
                       outer ? outer->verticalScrollBar()->value() : -1,
                       outer ? outer->verticalScrollBar()->maximum() : -1,
                       writer->verticalScrollBar()->value(),
                       writer->verticalScrollBar()->maximum());
          if (*at >= writerType.size()) {
            timer->stop();
            timer->deleteLater();
            delete at;
            QTimer::singleShot(50, grab);
          }
        });
        timer->start(60);
        return;
      }
    if (writerBodyLines > 0)
      if (writer) {
        QStringList lines;
        for (int i = 1; i <= writerBodyLines; ++i)
          lines << QStringLiteral(
                       "Paragraph %1. The fullscreen writer is the document "
                       "canvas, and the browser owns its outer scrollbar.")
                       .arg(i);
        QTextCursor c(writer->document());
        c.movePosition(QTextCursor::End);
        c.insertText(lines.join(QStringLiteral("\n\n")));
      }
    qApp->processEvents();
    // Page height lands via queued relayouts; wait one turn before clamping.
    if (scrollY >= 0 && find.isEmpty() && clickButton.isEmpty()) {
      QTimer::singleShot(50, [w, scrollY, grab] {
        if (auto* sa = qobject_cast<QScrollArea*>(w->centralWidget())) {
          sa->verticalScrollBar()->setValue(scrollY);
          auto* body = w->findChild<QTextEdit*>(
              QStringLiteral("cwbWriteEditor"));
          std::fprintf(stderr,
                       "windowshot layout: body=%d page=%d range=%d value=%d "
                       "blocks=%d\n",
                       body ? body->height() : -1,
                       sa->widget() ? sa->widget()->height() : -1,
                       sa->verticalScrollBar()->maximum(),
                       sa->verticalScrollBar()->value(),
                       body ? body->document()->blockCount() : -1);
        }
        grab();
      });
      return;
    }
    if (scrollY >= 0)
      if (auto* sa = qobject_cast<QScrollArea*>(w->centralWidget()))
        sa->verticalScrollBar()->setValue(scrollY);
    if (!find.isEmpty()) w->findInPage(find);
    if (!clickButton.isEmpty()) {
      if (auto* b = w->findChild<QAbstractButton*>(clickButton)) {
        if (holdButton) {
          b->setDown(true);  // capture the actual Qt :pressed visual state
          qApp->processEvents();
          grab();
          return;
        }
        for (int i = 0; i < (clickCount > 0 ? clickCount : 1); ++i) b->click();
      }
      QTimer::singleShot(clickWaitMs, grab);  // let the widget's async op finish
      return;
    }
    grab();
  });
  return qApp->exec();
}

// Regression: a relayout (width change) must keep the SAME editor widget with
// its text intact; a recreate returns a different pointer with empty text.
static int runWidgetReuseTest() {
  MainWindow w;
  w.disableAutoMine();
  w.resize(1000, 720);
  w.loadHtml(QStringLiteral(
      "<html><body><object type=\"application/x-cwb-write\" width=\"600\" "
      "height=\"400\"></object></body></html>"));
  w.show();
  qApp->processEvents();
  auto* edit = w.findChild<QTextEdit*>();
  if (!edit) {
    std::fprintf(stderr, "widgetreuse: FAIL (no editor created)\n");
    return 1;
  }
  const void* before = edit;
  edit->setPlainText(QStringLiteral("REUSE_MARKER_123"));
  qApp->processEvents();
  // The writer must mark the page dirty as soon as it has text -- the signal
  // the beforeunload guard reads to protect unsaved work.
  bool dirty = false;
  for (QWidget* cw : w.findChildren<QWidget*>())
    if (cw->property("cwbUnsaved").toBool()) dirty = true;
  w.resize(820, 720);  // width change -> relayout -> placeWidgets
  qApp->processEvents();
  auto* edit2 = w.findChild<QTextEdit*>();
  const bool same = (static_cast<const void*>(edit2) == before);
  const bool kept =
      edit2 && edit2->toPlainText() == QStringLiteral("REUSE_MARKER_123");
  std::fprintf(
      stderr,
      "widgetreuse: same_widget=%d text_preserved=%d dirty_flag=%d -> %s\n",
      same, kept, dirty, (same && kept && dirty) ? "PASS" : "FAIL");
  return (same && kept && dirty) ? 0 : 1;
}

// Regression: same-url navigate() must be a no-op (navGen_ unchanged); the
// combobox completer used to fire activated() with the loaded URL and wipe
// unsaved pages.
static int runNavGuardTest() {
  MainWindow w;
  w.disableAutoMine();
  const bool sameIgnored = w.testAddressNavIgnoredWhenSameAsCurrent();
  std::fprintf(stderr, "navguard: same_url_ignored=%d -> %s\n", sameIgnored,
               sameIgnored ? "PASS" : "FAIL");
  return sameIgnored ? 0 : 1;
}

static int runNavProgressTest() {
  MainWindow w;
  w.disableAutoMine();
  w.resize(1000, 720);
  w.show();
  qApp->processEvents();
  const bool ok = w.testNavigationProgress();
  std::fprintf(stderr,
               "navprogress: monotonic=1 old_page_retained=1 commit_finishes=%d -> %s\n",
               ok, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

static int runWriterDocumentTest() {
  MainWindow w;
  w.disableAutoMine();
  w.resize(760, 620);
  w.loadHtml(QStringLiteral(
      "<object type=\"application/x-cwb-write\"><param name=\"display\" "
      "value=\"fullscreen\"><param name=\"home\" "
      "value=\"lua://9@test.invalid:53831/\"></object>"));
  w.show();
  qApp->processEvents();
  auto* edit = w.findChild<QTextEdit*>(QStringLiteral("cwbWriteEditor"));
  if (!edit) return 1;
  const int hintY0 = edit->property("cwbBodyHintTop").toInt();
  auto pointerAt = [edit](QEvent::Type type, const QPoint& pos,
                          Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPoint global = edit->viewport()->mapToGlobal(pos);
    QMouseEvent event(type, QPointF(pos), QPointF(global), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(edit->viewport(), &event);
    qApp->processEvents();
  };
  pointerAt(QEvent::MouseMove, QPoint(40, 18), Qt::NoButton, Qt::NoButton);
  const int hintYTitleHover = edit->property("cwbBodyHintTop").toInt();
  pointerAt(QEvent::MouseMove, QPoint(40, 92), Qt::NoButton, Qt::NoButton);
  const int hintYBodyHover = edit->property("cwbBodyHintTop").toInt();
  pointerAt(QEvent::MouseButtonPress, QPoint(40, 92), Qt::LeftButton,
            Qt::LeftButton);
  pointerAt(QEvent::MouseButtonRelease, QPoint(40, 92), Qt::LeftButton,
            Qt::NoButton);
  const int hintYBodyClick = edit->property("cwbBodyHintTop").toInt();
  pointerAt(QEvent::MouseButtonPress, QPoint(40, 18), Qt::LeftButton,
            Qt::LeftButton);
  pointerAt(QEvent::MouseButtonRelease, QPoint(40, 18), Qt::LeftButton,
            Qt::NoButton);
  const int hintYTitleClick = edit->property("cwbBodyHintTop").toInt();
  const bool hintStable = hintY0 == hintYTitleHover &&
                          hintY0 == hintYBodyHover &&
                          hintY0 == hintYBodyClick && hintY0 == hintYTitleClick;
  auto* brand = w.findChild<QPushButton*>(QStringLiteral("cwbWriteBrand"));
  const bool brandLinked =
      brand && brand->isEnabled() && brand->cursor().shape() == Qt::PointingHandCursor;
  QTextCursor c(edit->document());
  c.insertText(QStringLiteral("Original title"));
  c.insertBlock();
  c.insertText(QStringLiteral("Body line"));
  qApp->processEvents();
  const int editorY = edit->mapTo(&w, QPoint()).y();
  c = QTextCursor(edit->document()->firstBlock());
  c.select(QTextCursor::BlockUnderCursor);
  c.removeSelectedText();
  edit->setTextCursor(c);
  qApp->processEvents();
  const int emptyPx = c.blockCharFormat().font().pixelSize();
  c.insertText(QStringLiteral("Replacement title"));
  qApp->processEvents();
  const int titlePx = QTextCursor(edit->document()->firstBlock())
                          .blockCharFormat().font().pixelSize();
  const int bodyPx = QTextCursor(edit->document()->firstBlock().next())
                         .blockCharFormat().font().pixelSize();
  const int bodyLineHeight =
      QTextCursor(edit->document()->firstBlock().next())
          .blockFormat()
          .lineHeight();
  const qreal paragraphGap =
      QTextCursor(edit->document()->firstBlock().next())
          .blockFormat()
          .bottomMargin();
  c = QTextCursor(edit->document()->firstBlock().next());
  const bool crossed = c.movePosition(QTextCursor::Up) && c.blockNumber() == 0;
  c = QTextCursor(edit->document());
  c.movePosition(QTextCursor::End);
  edit->setTextCursor(c);
  const int blocksBeforeDown = edit->document()->blockCount();
  QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
  QApplication::sendEvent(edit, &down);
  const bool downExtends =
      edit->document()->blockCount() == blocksBeforeDown + 1 &&
      edit->textCursor().blockNumber() == blocksBeforeDown;
  QApplication::sendEvent(edit, &down);
  QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
  QApplication::sendEvent(edit, &up);
  const bool oneSpeculativeLeft =
      edit->document()->blockCount() == blocksBeforeDown + 1;
  QApplication::sendEvent(edit, &up);
  const bool trailingCollected =
      edit->document()->blockCount() == blocksBeforeDown;
  const bool stable = edit->mapTo(&w, QPoint()).y() == editorY;
  const bool styled = emptyPx == 34 && titlePx == 34 && bodyPx == 21 &&
                      bodyLineHeight == 33 && paragraphGap == 18;
  std::fprintf(stderr,
               "writerdocument: empty=%d title=%d body=%d line=%d gap=%.0f "
               "crossed=%d down_extends=%d one_left=%d collected=%d brand=%d "
               "stable=%d hint=%d/%d/%d/%d/%d -> %s\n",
               emptyPx, titlePx, bodyPx, bodyLineHeight, paragraphGap, crossed,
               downExtends, oneSpeculativeLeft, trailingCollected, brandLinked,
               stable, hintY0, hintYTitleHover, hintYBodyHover, hintYBodyClick,
               hintYTitleClick,
               (styled && crossed && downExtends && oneSpeculativeLeft &&
                trailingCollected && brandLinked && stable && hintStable)
                   ? "PASS"
                   : "FAIL");
  return (styled && crossed && downExtends && oneSpeculativeLeft &&
          trailingCollected && brandLinked && stable && hintStable)
             ? 0
             : 1;
}

static int runRenderSelectionTest() {
  MainWindow window;
  window.disableAutoMine();
  window.resize(900, 700);
  window.loadHtml(QStringLiteral(R"HTML(
<!doctype html><html><head><style>
body{margin:0;background:#fff;color:#242424;font-family:serif}
#w{max-width:680px;margin:0 auto;padding:56px 24px 80px}
h1{font-size:42px;line-height:1.15;margin:0 0 14px}
.by{font-family:sans-serif;font-size:13px;margin:0 0 44px}
p{font-size:21px;line-height:1.58;margin:0 0 26px}
</style></head><body><div id=w><h1>Readable page</h1>
<div class=by>by A Writer</div>
<p>Alpha beta gamma delta epsilon zeta eta theta.</p>
<p>Second prose paragraph for browser selection.</p>
</div></body></html>)HTML"));
  window.show();
  qApp->processEvents();
  auto* scroll = qobject_cast<QScrollArea*>(window.centralWidget());
  auto* pane = scroll ? qobject_cast<RenderPane*>(scroll->widget()) : nullptr;
  if (!pane) return 1;
  pane->setFocus();
  pane->selectAllText();
  const QString all = pane->selectedText();
  QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
  QApplication::sendEvent(pane, &copy);
  const bool copied = QApplication::clipboard()->text() == all;
  const int before = all.size();
  QKeyEvent shrink(QEvent::KeyPress, Qt::Key_Left, Qt::ShiftModifier);
  QApplication::sendEvent(pane, &shrink);
  const bool shifted = pane->selectedText().size() == before - 1;
  const bool content = all.contains(QStringLiteral("Readable page")) &&
                       all.contains(QStringLiteral("Alpha beta gamma"));
  const bool lineBreaks =
      all.contains(QStringLiteral("Readable page\nby A Writer\nAlpha beta")) &&
      all.contains(QStringLiteral("theta.\nSecond prose paragraph"));
  pane->clearSelection();
  qApp->processEvents();
  const QImage unselected = pane->grab().toImage();

  // Send through whichever widget the real top-level hierarchy exposes at the
  // article coordinates, rather than cheating by calling RenderPane directly.
  const QPoint startInPane(150, 190);
  const QPoint endInPane(610, 190);
  const QPoint startGlobal = pane->mapToGlobal(startInPane);
  const QPoint endGlobal = pane->mapToGlobal(endInPane);
  QWidget* target = QApplication::widgetAt(startGlobal);
  if (!target) target = pane;
  auto local = [target](const QPoint& global) {
    return QPointF(target->mapFromGlobal(global));
  };
  QMouseEvent press(QEvent::MouseButtonPress, local(startGlobal), startGlobal,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &press);
  QMouseEvent move(QEvent::MouseMove, local(endGlobal), endGlobal, Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &move);
  QMouseEvent release(QEvent::MouseButtonRelease, local(endGlobal), endGlobal,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(target, &release);
  qApp->processEvents();
  const bool dragged = pane->hasSelection() && !pane->selectedText().isEmpty();
  const QImage selected = pane->grab().toImage();
  bool painted = false;
  if (unselected.size() == selected.size())
    for (int y = 0; y < selected.height() && !painted; ++y)
      for (int x = 0; x < selected.width(); ++x)
        if (unselected.pixel(x, y) != selected.pixel(x, y)) {
          painted = true;
          break;
        }
  std::fprintf(stderr,
               "renderselection: target=%s content=%d linebreaks=%d copied=%d shifted=%d "
               "dragged=%d painted=%d drag_text=\"%s\" -> %s\n",
               target->metaObject()->className(), content, lineBreaks, copied,
               shifted,
               dragged, painted, pane->selectedText().toUtf8().constData(),
               (content && lineBreaks && copied && shifted && dragged && painted)
                   ? "PASS"
                   : "FAIL");
  return (content && lineBreaks && copied && shifted && dragged && painted) ? 0
                                                                            : 1;
}

static int runWriterCursorTest() {
  MainWindow w;
  w.disableAutoMine();
  w.resize(760, 620);
  w.loadHtml(QStringLiteral(
      "<object type=\"application/x-cwb-write\"><param name=\"display\" "
      "value=\"fullscreen\"></object>"));
  w.show();
  qApp->processEvents();
  auto* edit = w.findChild<QTextEdit*>(QStringLiteral("cwbWriteEditor"));
  auto* outer = qobject_cast<QScrollArea*>(w.centralWidget());
  if (!edit || !outer) return 1;
  QStringList lines{QStringLiteral("Cursor traversal")};
  for (int i = 1; i <= 40; ++i)
    lines << QStringLiteral("Line %1: move through the native document.").arg(i);
  edit->setPlainText(lines.join(QLatin1Char('\n')));
  edit->setFocus();
  QTextCursor c(edit->document());
  c.movePosition(QTextCursor::End);
  edit->setTextCursor(c);
  for (int i = 0; i < 4; ++i) qApp->processEvents();
  std::fprintf(stderr,
               "layout model: editor=%dx%d viewport=%dx%d deltaH=%d "
               "doc=%.1fx%.1f innerRange=%d pageH=%d hostViewportH=%d "
               "outerRange=%d editorPageY=%d\n",
               edit->width(), edit->height(), edit->viewport()->width(),
               edit->viewport()->height(),
               edit->height() - edit->viewport()->height(),
               edit->document()->size().width(), edit->document()->size().height(),
               edit->verticalScrollBar()->maximum(), outer->widget()->height(),
               outer->viewport()->height(), outer->verticalScrollBar()->maximum(),
               edit->mapTo(outer->widget(), QPoint()).y());
  const int bottom = outer->verticalScrollBar()->value();
  int upMin = bottom;
  for (int i = 0; i < 45; ++i) {
    c = edit->textCursor();
    c.movePosition(QTextCursor::Up);
    edit->setTextCursor(c);
    qApp->processEvents();
    upMin = std::min(upMin, outer->verticalScrollBar()->value());
    std::fprintf(stderr,
                 "cursor up %02d block=%d outer=%d inner=%d caretY=%d\n", i,
                 c.blockNumber(), outer->verticalScrollBar()->value(),
                 edit->verticalScrollBar()->value(),
                 edit->viewport()->mapTo(outer->viewport(),
                                         edit->cursorRect().topLeft()).y());
  }
  for (int i = 0; i < 45; ++i) {
    c = edit->textCursor();
    c.movePosition(QTextCursor::Down);
    edit->setTextCursor(c);
    qApp->processEvents();
  }
  const int returned = outer->verticalScrollBar()->value();
  const bool pass = bottom > 0 && upMin < bottom && returned > upMin &&
                    edit->verticalScrollBar()->value() == 0 &&
                    edit->verticalScrollBar()->maximum() == 0;
  std::fprintf(stderr,
               "writercursor: bottom=%d upMin=%d returned=%d -> %s\n",
               bottom, upMin, returned, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// Time-lapse capture into numbered PNG frames (point outDir at /dev/shm):
// async flicker shows up as alternating frames.
static int runFilmstrip(const QString& outDir, const QString& openUrl,
                        int intervalMs, int durationMs) {
  QDir().mkpath(outDir);
  auto* w = new MainWindow();
  w->disableAutoMine();  // a capture harness must never start real mining
  w->resize(1000, 720);
  w->show();
  if (!openUrl.isEmpty()) w->openUrl(openUrl);
  auto* elapsed = new QElapsedTimer();
  elapsed->start();
  auto* frameNo = new int(0);
  auto* timer = new QTimer();
  QObject::connect(timer, &QTimer::timeout,
                   [w, outDir, elapsed, frameNo, durationMs, timer]() {
    const qint64 t = elapsed->elapsed();
    const QString path = QStringLiteral("%1/f%2_%3ms.png")
                             .arg(outDir)
                             .arg(*frameNo, 4, 10, QLatin1Char('0'))
                             .arg(t, 7, 10, QLatin1Char('0'));
    w->grab().save(path, "PNG");
    ++(*frameNo);
    if (t >= durationMs) {
      std::fprintf(stderr, "filmstrip: %d frames in %s\n", *frameNo,
                   outDir.toUtf8().constData());
      timer->stop();
      qApp->quit();
    }
  });
  timer->start(intervalMs);
  return qApp->exec();
}

// Headless capture of the mining cockpit. With --mine it actually mines the
// given server for --wait seconds (builds the RandomX dataset) then captures a
// live frame; otherwise it captures the idle layout at once.
static int runMineShot(const QString& out, const QString& mineServer,
                       int waitSec) {
  auto* engine = new cwb::MinerEngine(qApp);
  auto* w = new MiningWindow(engine);
  w->setInheritedServer(QStringLiteral("ces.pubcom.org"));
  w->show();
  if (auto* tb = w->findChild<QToolButton*>()) tb->setChecked(true);  // expand
  int delayMs = 300;
  if (!mineServer.isEmpty()) {
    w->startFromCli(mineServer);
    delayMs = std::max(1, waitSec) * 1000;
  }
  QTimer::singleShot(delayMs, [w, out]() {
    const QPixmap pm = w->grab();
    const bool ok = pm.save(out, "PNG");
    std::fprintf(stderr, ok ? "mineshot ok: %s (%dx%d)\n" : "mineshot FAILED\n",
                 out.toUtf8().constData(), pm.width(), pm.height());
    qApp->quit();
  });
  return qApp->exec();
}

// Render an app-icon SVG to a square PNG (for building the platform icon files
// or iterating on a design). Qt's SVG renderer, not ImageMagick's. `svgPath`
// empty = the embedded :/icons/cwb.svg; otherwise a file on disk.
static int runIconShot(const QString& out, int size, const QString& svgPath) {
  QSvgRenderer r(svgPath.isEmpty() ? QStringLiteral(":/icons/cwb.svg") : svgPath);
  if (!r.isValid()) {
    std::fprintf(stderr, "iconshot: bad svg\n");
    return 1;
  }
  QImage img(size, size, QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  r.render(&p);
  p.end();
  const bool ok = img.save(out, "PNG");
  std::fprintf(stderr, ok ? "iconshot ok: %s (%d)\n" : "iconshot FAILED\n",
               out.toUtf8().constData(), size);
  return ok ? 0 : 1;
}

static int runParseUrl(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  std::fprintf(stdout,
               "valid=%d scheme=%s main=%d mount=%s host=%s port=%u pid=%llu "
               "path=%s err=%s\n",
               u.valid, u.scheme.toUtf8().constData(), u.isMain,
               u.mount.toUtf8().constData(), u.host.toUtf8().constData(),
               static_cast<unsigned>(u.port),
               static_cast<unsigned long long>(u.pid),
               u.path.toUtf8().constData(), u.error.toUtf8().constData());
  return u.valid ? 0 : 1;
}

// lua:// HTTP fetch over CesPlex, prints the raw response.
static int runFetch(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || u.isMain || u.scheme != QLatin1String("lua")) {
    std::fprintf(stderr, "usage: fetch lua://<pid>@<host>:<port>/<path>\n");
    return 2;
  }
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  const std::string req = "GET " + u.path.toStdString() +
                          " HTTP/1.0\r\nHost: " + u.host.toStdString() +
                          "\r\nConnection: close\r\n\r\n";
  std::string resp;
  uint8_t st = 0xFF;
  const std::string err =
      cwb::cesLuaFetch(u.host.toStdString(), u.port, u.pid, id, req, resp, st);
  if (!err.empty()) {
    std::fprintf(stderr, "fetch error: %s (attach status=%u)\n", err.c_str(), st);
    return 1;
  }
  std::fwrite(resp.data(), 1, resp.size(), stdout);
  return 0;
}

// file:// read over /ces/file/1, prints the raw bytes.
static int runFileFetch(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || u.scheme != QLatin1String("file")) {
    std::fprintf(stderr, "usage: filefetch file://<host>:<port>/<path>\n");
    return 2;
  }
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  std::string out;
  const uint8_t code =
      cwb::cesFileFetch(u.host.toStdString(), u.port, id,
                        normalizeFileUrlPath(u.path).toStdString(), out);
  if (code != ces::CES_OK) {
    std::fprintf(stderr, "file fetch failed: %s\n", ces::errorString(code));
    return 1;
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}

// file:// streaming download to a local path: never buffers the file in RAM.
// Mirrors the browser's Download path, headless (no save dialog) so it is also
// how the streaming core is verified.
static int runDownload(const QString& urlStr, const QString& dest) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || u.isMain || u.scheme != QLatin1String("file")) {
    std::fprintf(stderr, "usage: download file://<host>:<port>/<path> <dest>\n");
    return 2;
  }
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  QFile out(dest);
  if (!out.open(QIODevice::WriteOnly)) {
    std::fprintf(stderr, "cannot write %s\n", dest.toUtf8().constData());
    return 1;
  }
  auto sink = [&out](const uint8_t* d, size_t n) -> bool {
    return out.write(reinterpret_cast<const char*>(d),
                     static_cast<qint64>(n)) == static_cast<qint64>(n);
  };
  auto prog = [](uint64_t done, uint64_t total) {
    std::fprintf(stderr, "\r%llu / %llu bytes",
                 static_cast<unsigned long long>(done),
                 static_cast<unsigned long long>(total));
  };
  const uint8_t rc =
      cwb::cesFileDownload(u.host.toStdString(), static_cast<uint16_t>(u.port),
                           id, normalizeFileUrlPath(u.path).toStdString(),
                           sink, prog);
  out.close();
  std::fprintf(stderr, "\n");
  if (rc != ces::CES_OK) {
    out.remove();  // drop the partial file
    std::fprintf(stderr, "download failed: rc=%u\n", static_cast<unsigned>(rc));
    return 1;
  }
  std::fprintf(stderr, "saved %s\n", dest.toUtf8().constData());
  return 0;
}

// compute:// instance directory over /ces/compute/1, prints the generated HTML.
static int runComputeFetch(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || u.scheme != QLatin1String("compute")) {
    std::fprintf(stderr,
                 "usage: computefetch compute://<host>:<port>/<source>\n");
    return 2;
  }
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  std::string out;
  const uint8_t code = cwb::cesComputeFetch(u.host.toStdString(), u.port, id,
                                            u.path.toStdString(), out);
  if (code != ces::CES_OK) {
    std::fprintf(stderr, "compute query failed: %s\n", ces::errorString(code));
    return 1;
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}

// ces:// account query over the main UDP port, prints the generated HTML.
static int runAccountFetch(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || !u.isMain) {
    std::fprintf(stderr,
                 "usage: accountfetch ces://<host>:<mainport>/[pubkey]\n");
    return 2;
  }
  QString pk = u.path;
  if (pk.startsWith('/')) pk = pk.mid(1);
  std::string out;
  const uint8_t code = cwb::cesAccountFetch(u.host.toStdString(), u.port,
                                            pk.toStdString(), out);
  if (code != ces::CES_OK) {
    std::fprintf(stderr, "account query failed: %s\n", ces::errorString(code));
    return 1;
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}

// squery: signed (server-verified) account query.
static int runSquery(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || !u.isMain) {
    std::fprintf(stderr, "usage: squery ces://<host>:<mainport>/[pubkey]\n");
    return 2;
  }
  QString pk = u.path;
  if (pk.startsWith('/')) pk = pk.mid(1);
  std::string out;
  const uint8_t code = cwb::cesSquery(u.host.toStdString(),
                                      static_cast<uint16_t>(u.port),
                                      pk.toStdString(), out);
  if (code != ces::CES_OK) {
    std::fprintf(stderr, "squery failed: %s\n", ces::errorString(code));
    return 1;
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}

static int runWhoami() {
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  // stdout: just the pubkey, so `$(cwb whoami)` is a clean 64-hex capture.
  std::fprintf(stdout, "%s\n", id.getPublicKeyHexStr().c_str());
  const QString name =
      cwb::prettyName(cwb::canonicalKeyName(cwb::preferredName()));
  if (!name.isEmpty())  // human info on stderr, out of the machine lane
    std::fprintf(stderr, "name: %s\n", name.toUtf8().constData());
  return 0;
}

// Register (or rename) our name on a server (the CLI form of the identity
// widget). One key, one name; the ledger enforces uniqueness both ways.
static int runIdentitySet(const QString& urlStr, const QString& name) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || !u.isMain) {
    std::fprintf(stderr, "need a ces://host:mainport/ address\n");
    return 2;
  }
  cwb::NameStatus st;
  cwb::registerName(u.host, static_cast<uint16_t>(u.port), name, st);
  std::fprintf(st.ok ? stdout : stderr, "%s\n", st.detail.toUtf8().constData());
  return st.ok ? 0 : 1;
}

// Run the boot maintenance pass once on a server and report (name confirm,
// story upkeep). The scriptable form of what the browser does on open.
static int runIdentityCheck(const QString& urlStr) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || !u.isMain) {
    std::fprintf(stderr, "need a ces://host:mainport/ address\n");
    return 2;
  }
  cwb::NameStatus st;
  cwb::maintainName(u.host, static_cast<uint16_t>(u.port), st);
  std::fprintf(stdout, "name: %s\nhandle: %s\nserver: %s\nregistered: %s\n"
                       "detail: %s\n",
               st.name.toUtf8().constData(), st.handle.toUtf8().constData(),
               st.server.toUtf8().constData(), st.ok ? "yes" : "no",
               st.detail.toUtf8().constData());
  return st.ok ? 0 : 1;
}

// Resolve the name registered to a pubkey on a server (ledger-verified).
static int runResolveName(const QString& urlStr, const QString& pubkeyHex) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || !u.isMain) {
    std::fprintf(stderr, "need a ces://host:mainport/ address\n");
    return 2;
  }
  const QString name =
      cwb::resolveName(u.host, static_cast<uint16_t>(u.port), pubkeyHex);
  if (name.isEmpty()) {
    std::fprintf(stderr, "unnamed\n");
    return 1;
  }
  std::fprintf(stdout, "%s\n", name.toUtf8().constData());
  return 0;
}

// Open a persistent duplex lua:// session, print the greeting, optionally send a
// line, then ~2 s of stream. The CLI proof of the terminal transport.
static int runDialHarness(const QString& urlStr, const QString& send) {
  const CesUrl u = parseCesUrl(urlStr);
  if (!u.valid || u.isMain || u.scheme != QLatin1String("lua")) {
    std::fprintf(stderr, "usage: dial lua://<pid>@<host>:<port>/ [--send text]\n");
    return 2;
  }
  const ces::KeyPair id = cwb::loadOrCreateIdentity();
  cwb::CesLuaSession sess;
  auto buf = std::make_shared<std::string>();
  auto mu = std::make_shared<std::mutex>();
  sess.onData([buf, mu](const std::string& s) {
    std::lock_guard<std::mutex> lk(*mu);
    *buf += s;
  });
  std::string hello;
  uint8_t st = 0xFF;
  const std::string err =
      sess.open(u.host.toStdString(), u.port, u.pid, id, hello, st);
  if (!err.empty()) {
    std::fprintf(stderr, "dial error: %s (attach status=%u)\n", err.c_str(), st);
    return 1;
  }
  std::fprintf(stdout, "=== HELLO (%zu bytes) ===\n%s=== STREAM ===\n",
               hello.size(), hello.c_str());
  if (!send.isEmpty()) sess.write(send.toStdString() + "\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  {
    std::lock_guard<std::mutex> lk(*mu);
    std::fwrite(buf->data(), 1, buf->size(), stdout);
  }
  sess.close();
  return 0;
}

int main(int argc, char** argv) {
  // Wayland: the default decoration bleeds into a white desktop; Adwaita
  // draws the standard GNOME frame. Must precede QApplication; user override
  // wins; ignored off Wayland.
  if (qEnvironmentVariableIsEmpty("QT_WAYLAND_DECORATION"))
    qputenv("QT_WAYLAND_DECORATION", "adwaita");
  QApplication app(argc, argv);
  QApplication::setOrganizationName("ces");
  QApplication::setApplicationName("cwb");
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/cwb.svg")));
  // GNOME docks ignore the icon hint and match app_id to cwb.desktop.
  QGuiApplication::setDesktopFileName(QStringLiteral("cwb"));

  // Parse our own args with CLI11 over what Qt left behind (Qt strips its own
  // -platform/-style flags first). No subcommand -> open the browser.
  const QStringList qargs = app.arguments();
  std::vector<std::string> store;
  store.reserve(qargs.size());
  for (const QString& a : qargs) store.push_back(a.toStdString());
  std::vector<char*> cv;
  cv.reserve(store.size());
  for (std::string& s : store) cv.push_back(s.data());

  CLI::App cli{"cwb - the CES web browser. With no arguments it opens a window."};
  cli.require_subcommand(0, 1);

  std::string url;
  cli.add_option("url", url,
                 "CES address to open in a window "
                 "(file:// compute:// lua:// luarpc:// ces://)");
  std::string htmlFile;
  cli.add_option("--html", htmlFile, "load a local HTML file into the window");
  std::string mineServer;
  cli.add_option("--mine", mineServer,
                 "open the mining cockpit and start mining this server");

  bool testPaths = false;
  cli.add_flag("--testpaths", testPaths,
               "use the Qt test data location for keys/settings (harness)");

  // Self-recording: grab the window into a RAM ring and serve a localhost query
  // API (flicker detection + frame pull) -- no disk. Opt-in.
  bool record = false;
  int recordMs = 200, recordMb = 512;
  int recordPort = 8091;
  cli.add_flag("--record", record,
               "record the window to a RAM ring + serve a localhost query API "
               "(flicker detection; no disk)");
  cli.add_option("--record-ms", recordMs, "ms between recorded frames (default 200)");
  cli.add_option("--record-mb", recordMb, "RAM cap for the frame ring in MB (default 512)");
  cli.add_option("--record-port", recordPort, "localhost query port (default 8091)");

  auto* scWhoami =
      cli.add_subcommand("whoami", "print this browser's identity pubkey");

  std::string isUrl, isName;
  auto* scIdSet = cli.add_subcommand(
      "identityset", "register (or rename) your name on a server");
  scIdSet->add_option("url", isUrl, "ces://host:mainport/")->required();
  scIdSet->add_option("name", isName, "your name (1..32 bytes)")->required();
  std::string icUrl;
  auto* scIdCheck = cli.add_subcommand(
      "identitycheck", "confirm your name + feed your stories once");
  scIdCheck->add_option("url", icUrl, "ces://host:mainport/")->required();
  std::string rnUrl, rnKey;
  auto* scResolve = cli.add_subcommand(
      "resolvename", "resolve a pubkey's registered name");
  scResolve->add_option("url", rnUrl, "ces://host:mainport/")->required();
  scResolve->add_option("pubkey", rnKey, "64-hex public key")->required();

  std::string pUrl;
  auto* scParse =
      cli.add_subcommand("parseurl", "parse a CES URL and print its parts");
  scParse->add_option("url", pUrl, "the URL")->required();

  std::string fUrl;
  auto* scFetch =
      cli.add_subcommand("fetch", "lua:// fetch over CesPlex, print the response");
  scFetch->add_option("url", fUrl, "lua://<pid>@<host>:<port>/<path>")->required();

  std::string ffUrl;
  auto* scFile = cli.add_subcommand("filefetch", "file:// read, print the bytes");
  scFile->add_option("url", ffUrl, "file://<host>:<port>/<path>")->required();

  std::string dlUrl, dlDest;
  auto* scDl =
      cli.add_subcommand("download", "file:// stream to a local path (no RAM buffer)");
  scDl->add_option("url", dlUrl, "file://<host>:<port>/<path>")->required();
  scDl->add_option("dest", dlDest, "output file path")->required();

  std::string cfUrl;
  auto* scCompute = cli.add_subcommand(
      "computefetch", "compute:// instance directory, print the HTML");
  scCompute->add_option("url", cfUrl, "compute://<host>:<port>/<source>")
      ->required();

  std::string afUrl;
  auto* scAccount =
      cli.add_subcommand("accountfetch", "ces:// account query, print the HTML");
  scAccount->add_option("url", afUrl, "ces://<host>:<mainport>/[pubkey]")
      ->required();

  std::string sqUrl;
  auto* scSquery = cli.add_subcommand(
      "squery", "signed (server-verified) account query, print the result");
  scSquery->add_option("url", sqUrl, "ces://<host>:<mainport>/[pubkey]")
      ->required();

  std::string goUrl;
  auto* scGo = cli.add_subcommand(
      "go", "open a window and browse to a CES address (like xdg-open)");
  scGo->add_option("url", goUrl, "the CES address to open")->required();

  std::string dUrl, dSend;
  auto* scDial =
      cli.add_subcommand("dial", "duplex lua:// session, print the stream");
  scDial->add_option("url", dUrl, "lua://<pid>@<host>:<port>/")->required();
  scDial->add_option("--send", dSend, "send this line after connecting");

  std::string shotOut, shotHtml;
  int shotWidth = 900;
  double shotZoom = 1.0;
  auto* scShot =
      cli.add_subcommand("shot", "headless render an HTML page to a PNG");
  scShot->add_option("out", shotOut, "output PNG")->required();
  scShot->add_option("--html", shotHtml, "HTML file to render (else the sample)");
  scShot->add_option("--width", shotWidth, "render width in px");
  scShot->add_option("--zoom", shotZoom, "reflow zoom factor (1.0 = 100%)");

  std::string wsOut, wsOpen, wsHtml, wsFind, wsClick, wsWriterTitle,
      wsWriterType;
  int wsClickWait = 2500;
  int wsClicks = 1;
  bool wsHold = false;
  int wsBodyLines = 0, wsScrollY = -1, wsWidth = 1000, wsHeight = 720;
  auto* scWin =
      cli.add_subcommand("windowshot", "capture the whole window to a PNG");
  scWin->add_option("out", wsOut, "output PNG")->required();
  scWin->add_option("--open", wsOpen, "open this URL before capturing");
  scWin->add_option("--html", wsHtml, "load this HTML file before capturing");
  scWin->add_option("--find", wsFind, "open find-in-page for this text before capturing");
  scWin->add_option("--click", wsClick, "click the child button with this objectName, then capture");
  scWin->add_option("--clickwait", wsClickWait, "ms to wait after --click before capturing (default 2500)");
  scWin->add_option("--clicks", wsClicks, "how many times to click --click (2 = arm+confirm; default 1)");
  scWin->add_flag("--hold", wsHold,
                  "capture --click button while held down (no activation)");
  scWin->add_option("--writer-title", wsWriterTitle,
                    "set the fullscreen writer title before capture");
  scWin->add_option("--writer-type", wsWriterType,
                    "type body text incrementally before capture");
  scWin->add_option("--writer-lines", wsBodyLines,
                    "fill the fullscreen writer with N test paragraphs");
  scWin->add_option("--scroll-y", wsScrollY,
                    "set the browser's outer page scroll position");
  scWin->add_option("--width", wsWidth, "window width (default 1000)");
  scWin->add_option("--height", wsHeight, "window height (default 720)");

  std::string fsOut, fsOpen;
  int fsInterval = 500, fsDuration = 15000;
  auto* scFilm = cli.add_subcommand(
      "filmstrip", "grab the window every --interval ms for --duration ms "
                   "(captures async transitions; point outdir at /dev/shm)");
  scFilm->add_option("outdir", fsOut, "output directory for frames")->required();
  scFilm->add_option("--open", fsOpen, "open this URL, then film the transitions");
  scFilm->add_option("--interval", fsInterval, "ms between frames (default 500)");
  scFilm->add_option("--duration", fsDuration, "total ms to capture (default 15000)");

  auto* scReuse = cli.add_subcommand(
      "selftest-widgetreuse",
      "headless: assert the embedded editor survives a relayout (text + focus)");
  auto* scNavGuard = cli.add_subcommand(
      "selftest-navguard",
      "headless: assert a re-nav to the current URL is ignored (no text wipe)");
  auto* scNavProgress = cli.add_subcommand(
      "selftest-navprogress",
      "headless: assert navigation progress retains old content until commit");
  auto* scWriterDocument = cli.add_subcommand(
      "selftest-writerdocument",
      "headless: assert title deletion/style and unified cursor traversal");
  auto* scWriterCursor = cli.add_subcommand(
      "selftest-writercursor",
      "headless: trace outer scrolling while the caret moves up and down");
  auto* scRenderSelection = cli.add_subcommand(
      "selftest-renderselection",
      "headless: assert HTML select-all, copy, and shift-selection");

  std::string icoOut;
  int icoSize = 256;
  auto* scIcon =
      cli.add_subcommand("iconshot", "render the app icon SVG to a PNG");
  scIcon->add_option("out", icoOut, "output PNG")->required();
  scIcon->add_option("--size", icoSize, "square size in px");
  std::string icoSvg;
  scIcon->add_option("--svg", icoSvg, "render this SVG file instead of the embedded one");

  std::string msOut, msServer;
  int msWait = 90;
  auto* scMine =
      cli.add_subcommand("mineshot", "capture the mining cockpit to a PNG");
  scMine->add_option("out", msOut, "output PNG")->required();
  scMine->add_option("--mine", msServer, "actually mine this server before capturing");
  scMine->add_option("--wait", msWait, "seconds to mine before capturing");

  try {
    cli.parse(static_cast<int>(cv.size()), cv.data());
  } catch (const CLI::ParseError& e) {
    return cli.exit(e);
  }

  if (testPaths) QStandardPaths::setTestModeEnabled(true);

  if (*scWhoami) return runWhoami();
  if (*scIdSet)
    return runIdentitySet(QString::fromStdString(isUrl),
                          QString::fromStdString(isName));
  if (*scIdCheck) return runIdentityCheck(QString::fromStdString(icUrl));
  if (*scResolve)
    return runResolveName(QString::fromStdString(rnUrl),
                          QString::fromStdString(rnKey));
  if (*scParse) return runParseUrl(QString::fromStdString(pUrl));
  if (*scFetch) return runFetch(QString::fromStdString(fUrl));
  if (*scFile) return runFileFetch(QString::fromStdString(ffUrl));
  if (*scDl)
    return runDownload(QString::fromStdString(dlUrl),
                       QString::fromStdString(dlDest));
  if (*scCompute) return runComputeFetch(QString::fromStdString(cfUrl));
  if (*scAccount) return runAccountFetch(QString::fromStdString(afUrl));
  if (*scSquery) return runSquery(QString::fromStdString(sqUrl));
  if (*scDial)
    return runDialHarness(QString::fromStdString(dUrl),
                          QString::fromStdString(dSend));
  if (*scShot)
    return runShot(QString::fromStdString(shotOut),
                   QString::fromStdString(shotHtml), shotWidth, shotZoom);
  if (*scIcon)
    return runIconShot(QString::fromStdString(icoOut), icoSize,
                       QString::fromStdString(icoSvg));
  if (*scMine)
    return runMineShot(QString::fromStdString(msOut),
                       QString::fromStdString(msServer), msWait);
  if (*scFilm)
    return runFilmstrip(QString::fromStdString(fsOut),
                        QString::fromStdString(fsOpen), fsInterval, fsDuration);
  if (*scReuse) return runWidgetReuseTest();
  if (*scNavGuard) return runNavGuardTest();
  if (*scNavProgress) return runNavProgressTest();
  if (*scWriterDocument) return runWriterDocumentTest();
  if (*scWriterCursor) return runWriterCursorTest();
  if (*scRenderSelection) return runRenderSelectionTest();
  if (*scWin)
    return runWindowShot(QString::fromStdString(wsOut),
                         QString::fromStdString(wsOpen),
                         QString::fromStdString(wsHtml),
                         QString::fromStdString(wsFind),
                         QString::fromStdString(wsClick), wsClickWait, wsClicks,
                         wsHold,
                         QString::fromStdString(wsWriterTitle),
                         QString::fromStdString(wsWriterType), wsBodyLines,
                         wsScrollY, wsWidth, wsHeight);
  if (*scGo) url = goUrl;  // `go <url>`: fall through and open a window there

  // No subcommand (or `go <url>`): open the browser window.
  MainWindow w;
  if (!htmlFile.empty()) {
    QFile f(QString::fromStdString(htmlFile));
    if (f.open(QIODevice::ReadOnly)) w.loadHtml(QString::fromUtf8(f.readAll()));
  }
  w.show();
  if (record) {  // self-recording: RAM ring + localhost query API, no disk
    auto* rec = new FrameRecorder(&w, recordMs,
                                  static_cast<qint64>(recordMb) * 1024 * 1024,
                                  static_cast<quint16>(recordPort), &w);
    rec->start();
  }
  // Keep your name alive and feed your stories, in the background, every boot.
  // Best-effort, against the home server (the current server's own poll keeps
  // maintaining as you browse).
  if (!cwb::preferredName().isEmpty()) {
    QString homeHost;
    {
      CwbSettings s;
      const CesUrl hu = parseCesUrl(s.homePage());
      if (hu.valid && !hu.host.isEmpty()) homeHost = hu.host;
    }
    if (!homeHost.isEmpty()) {
      QPointer<MainWindow> wp(&w);
      std::thread([wp, homeHost]() {
        try {
          cwb::NameStatus st;
          cwb::maintainName(homeHost, kCesMainPort, st);
          const QString msg =
              st.ok ? QStringLiteral("name: %1 (%2)").arg(st.name, st.detail)
                    : QStringLiteral("NAME NEEDS ATTENTION: %1").arg(st.detail);
          QMetaObject::invokeMethod(
              qApp,
              [wp, msg]() {
                if (wp) wp->statusBar()->showMessage(msg, 15000);
              },
              Qt::QueuedConnection);
        } catch (...) {
        }
      }).detach();
    }
  }
  if (!url.empty()) {
    const QString u = QString::fromStdString(url);
    QTimer::singleShot(0, &w, [&w, u]() { w.openUrl(u); });
  } else if (htmlFile.empty() && mineServer.empty()) {
    // No destination: open home. Restore-session already queued its own
    // navigation and wins.
    QTimer::singleShot(0, &w, [&w]() {
      CwbSettings s;
      if (!(s.restoreSession() && !s.lastUrl().isEmpty()))
        w.openUrl(s.homePage());
    });
  }
  if (!mineServer.empty()) {
    const QString s = QString::fromStdString(mineServer);
    QTimer::singleShot(0, &w, [&w, s]() { w.openMiningAndStart(s); });
  }
  // Quit cleanly on SIGINT/SIGTERM (so kill/pkill closes the window instead of
  // needing SIGKILL): the handler only flips an atomic; a timer polls it.
  static std::atomic<bool> quitReq{false};
  std::signal(SIGINT, [](int) { quitReq.store(true); });
  std::signal(SIGTERM, [](int) { quitReq.store(true); });
  QTimer sigPoll;
  QObject::connect(&sigPoll, &QTimer::timeout, []() {
    if (quitReq.load()) qApp->quit();
  });
  sigPoll.start(150);
  return app.exec();
}
