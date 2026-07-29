// The writing surface: typographic editor + one green Publish. Publishing
// typesets the story, uploads it to /f/<name>/<app>/<slug>.html with a rent
// deposit, and navigates to the live address.
// Page-supplied params: approot (REQUIRED to publish), appname, appsource,
// authorhandle, author, title, body, name (slug override), host, rpcport
// (a page served over luarpc:// MUST declare rpcport: its own address names
// the instance's port, not the server lane).
#include "../widget.h"
#include "confirm.h"
#include "style.h"

#include "../cesdial.h"
#include "../credits.h"
#include "../names.h"
#include "../worktree.h"

#include <ces/keys.h>
#include <ces/l2/compute_client.h>
#include <ces/l2/file_client.h>
#include <ces/types.h>

#include <QApplication>
#include <QColor>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QTextEdit>
#include <QAbstractTextDocumentLayout>
#include <QWheelEvent>
#include <QPointer>
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <ctime>
#include <functional>
#include <string>
#include <thread>

namespace {

// One type system, both sides. The editor uses it as QFont; the published
// article carries it as CSS, so reading looks exactly like writing.
const char* kSerifCss =
    "Charter,'Bitstream Charter',Georgia,'Noto Serif','Liberation Serif',serif";

QString htmlEsc(QString s) {
  s.replace('&', QStringLiteral("&amp;"));
  s.replace('<', QStringLiteral("&lt;"));
  s.replace('>', QStringLiteral("&gt;"));
  return s;
}

QString htmlAttrEsc(QString s) {
  s = htmlEsc(std::move(s));
  s.replace('"', QStringLiteral("&quot;"));
  s.replace('\'', QStringLiteral("&#39;"));
  return s;
}

QString todayUtc() {
  char date[20] = "";
  const std::time_t t = std::time(nullptr);
  std::tm tmv{};
  gmtime_r(&t, &tmv);
  std::strftime(date, sizeof date, "%Y-%m-%d", &tmv);
  return QLatin1String(date);
}

// The published artifact: a typeset article; reader typography == editor
// typography. Footer links the live shelf via the stable dynamic-app address
// (compute://host/s/<app>.lua/by/<name>); the x-cwb-story vitals bar rides
// below it.
std::string articleHtml(const QString& titleFlow, const QString& bodyFlow,
                        const std::string& authorHex,
                        const QString& authorName, const QString& product,
                        const QString& appHome, const std::string& zonePath,
                        const QString& rawTitle, const QString& appSource,
                        const QString& authorPage) {
  const QString author =
      authorName.isEmpty()
          ? "<b class=hx>" + QString::fromStdString(authorHex.substr(0, 16)) +
                "&#8230;</b>"
          : "<b>" + htmlEsc(authorName) + "</b>";
  const QString appLink =
      appHome.isEmpty()
          ? QStringLiteral("<span class=vm>%1</span>").arg(htmlEsc(product))
          : QStringLiteral("<a class=vm href=\"%1\">%2</a>")
                .arg(htmlAttrEsc(appHome), htmlEsc(product));
  QString page = QStringLiteral(
      "<!doctype html><html><head><meta charset=utf-8><style>\n"
      "body{margin:0;background:#ffffff;color:#242424;font-family:%1}\n"
      "#w{max-width:700px;margin:0 auto;padding:56px 28px 80px}\n"
      "h1{font-size:34px;line-height:46px;font-weight:700;margin:0 0 16px}\n"
      ".by{font-family:sans-serif;font-size:13px;color:#6b6b6b;"
      "margin:0 0 44px}.by b{color:#242424;font-weight:600}"
      ".by .hx{font-family:monospace}\n"
      "p{font-size:21px;line-height:33px;margin:0 0 18px}\n"
      "p a{color:#1a8917;text-decoration:none}\n"
      ".ft{font-family:sans-serif;font-size:13px;color:#6b6b6b;"
      "margin-top:56px;border-top:1px solid #ececeb;padding-top:16px}\n"
      ".ft a{color:#1a8917;text-decoration:none}\n"
      ".vm{font-family:sans-serif;font-size:12px;letter-spacing:3px;"
      "color:#b3b3b1}\n"
      "</style></head><body><div id=w>\n"
      "<h1>%2</h1>\n<div class=by>by %3 &#183; %4</div>\n"
      "%5"
      "<div class=ft>%6%7</div>\n")
                     .arg(QLatin1String(kSerifCss), titleFlow, author,
                          todayUtc(), bodyFlow,
                          authorPage.isEmpty()
                              ? QString()
                              : QStringLiteral(
                                    "<a href=\"%1\">More stories by this "
                                    "author</a> &nbsp;&#183;&nbsp; ")
                                    .arg(htmlAttrEsc(authorPage)),
                          appLink);
  page += QStringLiteral(
              "<object type=\"application/x-cwb-story\" width=\"644\" "
              "height=\"150\">"
              "<param name=\"path\" value=\"%1\">"
              "<param name=\"appname\" value=\"%2\">"
              "<param name=\"home\" value=\"%3\">"
              "<param name=\"appsource\" value=\"%4\">"
              "<param name=\"title\" value=\"%5\"></object>\n"
              "</div></body></html>\n")
              .arg(htmlAttrEsc(QString::fromStdString(zonePath)),
                   htmlAttrEsc(product), htmlAttrEsc(appHome),
                   htmlAttrEsc(appSource), htmlAttrEsc(rawTitle));
  return page.toStdString();
}

class WriteEditor : public QTextEdit {
 public:
  WriteEditor(const QFont& titleFont, const QFont& bodyFont, QWidget* parent)
      : QTextEdit(parent), titleFont_(titleFont), bodyFont_(bodyFont) {
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    document()->setDefaultFont(bodyFont_);
  }

  std::function<void()> widthChanged;

  QString title() const { return document()->firstBlock().text().trimmed(); }

  QString body() const {
    QStringList lines;
    for (QTextBlock b = document()->firstBlock().next(); b.isValid();
         b = b.next())
      lines << b.text();
    return lines.join(QLatin1Char('\n')).trimmed();
  }

  QString titleFlowHtml() const {
    return document()->firstBlock().text().toHtmlEscaped();
  }

  QString bodyFlowHtml() const {
    QString out;
    for (QTextBlock block = document()->firstBlock().next(); block.isValid();
         block = block.next()) {
      if (block.text().trimmed().isEmpty()) continue;
      out += QStringLiteral("<p>") + block.text().toHtmlEscaped() +
             QStringLiteral("</p>\n");
    }
    return out;
  }

  int bodyHintTop() const {
    const QTextBlock first = document()->firstBlock();
    const QRectF firstBox =
        document()->documentLayout()->blockBoundingRect(first);
    // QTextEdit hides its doc-to-viewport offset; read it once, then keep it
    // independent of Qt's mutable insertion-cursor geometry.
    if (titleViewportTop_ < 0)
      titleViewportTop_ = cursorRect(QTextCursor(first)).top();
    if (const QTextBlock second = first.next(); second.isValid()) {
      const QRectF secondBox =
          document()->documentLayout()->blockBoundingRect(second);
      return titleViewportTop_ +
             static_cast<int>(std::round(secondBox.top() - firstBox.top()));
    }

    // No body block yet: derive its start from the title's laid-out box
    // (grows per wrapped line; fontMetrics().height() is one line only).
    const qreal titleHeight = first.text().isEmpty()
                                  ? first.blockFormat().lineHeight()
                                  : firstBox.height();
    return titleViewportTop_ + static_cast<int>(std::ceil(titleHeight)) +
           static_cast<int>(std::ceil(first.blockFormat().bottomMargin()));
  }

  void setParts(const QString& title, const QString& body) {
    setPlainText(body.isEmpty() ? title : title + QLatin1Char('\n') + body);
    applyBlockStyles();
    QTextCursor c(document());
    c.movePosition(QTextCursor::Start);
    setTextCursor(c);
  }

  void applyBlockStyles() {
    if (formatting_) return;
    formatting_ = true;
    const QTextCursor saved = textCursor();
    bool first = true;
    for (QTextBlock block = document()->begin(); block.isValid();
         block = block.next()) {
      QTextCursor c(block);
      QTextCharFormat format;
      format.setFont(first ? titleFont_ : bodyFont_);
      // BlockCharFormat survives deleting the title (a selection format does
      // not).
      c.setBlockCharFormat(format);
      QTextBlockFormat blockFormat = c.blockFormat();
      // Two vertical rhythms: line leading within a paragraph, block margin
      // between paragraphs.
      blockFormat.setLineHeight(first ? 46 : 33,
                                QTextBlockFormat::FixedHeight);
      blockFormat.setBottomMargin(first ? 16 : 18);
      c.setBlockFormat(blockFormat);
      first = false;
    }
    setTextCursor(saved);
    QTextCharFormat insertion;
    insertion.setFont(saved.blockNumber() == 0 ? titleFont_ : bodyFont_);
    setCurrentCharFormat(insertion);
    formatting_ = false;
    viewport()->update();
  }

 protected:
  void keyPressEvent(QKeyEvent* e) override {
    const bool plainDown =
        e->key() == Qt::Key_Down &&
        !(e->modifiers() &
          (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
           Qt::MetaModifier));
    const bool plainUp =
        e->key() == Qt::Key_Up &&
        !(e->modifiers() &
          (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
           Qt::MetaModifier));
    const QTextCursor before = textCursor();
    QTextEdit::keyPressEvent(e);

    // Down at the document bottom materializes the next paragraph.
    if (plainDown && textCursor().position() == before.position() &&
        before.atEnd()) {
      QTextCursor next = textCursor();
      next.insertBlock();
      setTextCursor(next);
      applyBlockStyles();
    }

    // Up collects the empty tail blocks Down created, so casual navigation
    // never permanently grows the article.
    if (plainUp) {
      QTextCursor keep = textCursor();
      bool collected = false;
      while (document()->lastBlock() != keep.block() &&
             document()->lastBlock().text().isEmpty()) {
        QTextCursor tail(document());
        tail.movePosition(QTextCursor::End);
        tail.deletePreviousChar();  // remove the final empty block separator
        collected = true;
      }
      if (collected) {
        setTextCursor(keep);
        applyBlockStyles();
      }
    }
  }

  void wheelEvent(QWheelEvent* e) override {
    // The fullscreen-widget host owns the page scrollbar.
    e->ignore();
  }

  void resizeEvent(QResizeEvent* e) override {
    const int oldWidth = viewport()->width();
    QTextEdit::resizeEvent(e);
    if (viewport()->width() != oldWidth && widthChanged)
      QTimer::singleShot(0, this, [this] {
        if (widthChanged) widthChanged();
      });
  }

  void paintEvent(QPaintEvent* e) override {
    QTextEdit::paintEvent(e);
    QPainter painter(viewport());
    painter.setPen(QColor(QStringLiteral("#b3b3b1")));

    const QTextBlock first = document()->firstBlock();
    if (first.text().isEmpty()) {
      QTextCursor c(first);
      const QRect caret = cursorRect(c);
      painter.setFont(titleFont_);
      painter.drawText(caret.left(),
                       caret.top() + QFontMetrics(titleFont_).ascent(),
                       tr("Type title here"));
    }

    bool bodyEmpty = true;
    for (QTextBlock b = first.next(); b.isValid(); b = b.next())
      if (!b.text().trimmed().isEmpty()) {
        bodyEmpty = false;
        break;
      }
    if (bodyEmpty) {
      const int y = bodyHintTop();
      setProperty("cwbBodyHintTop", y);  // regression/visual harness seam
      painter.setFont(bodyFont_);
      painter.drawText(0, y + QFontMetrics(bodyFont_).ascent(),
                       tr("Tell your story…"));
    }
  }

 private:
  QString visualLinesHtml(const QTextBlock& block) const {
    if (!block.isValid()) return {};
    const QString text = block.text();
    const QTextLayout* layout = block.layout();
    if (!layout || layout->lineCount() == 0)
      return QStringLiteral("<span class=vl>%1</span>").arg(htmlEsc(text));
    QString out;
    for (int i = 0; i < layout->lineCount(); ++i) {
      const QTextLine line = layout->lineAt(i);
      const QString part =
          text.mid(line.textStart(), line.textLength()).trimmed();
      if (i) out += QStringLiteral("<br>");
      out += QStringLiteral("<span class=vl>%1</span>")
                 .arg(part.isEmpty() ? QStringLiteral("&nbsp;")
                                     : htmlEsc(part));
    }
    return out;
  }

  QFont titleFont_;
  QFont bodyFont_;
  bool formatting_ = false;
  mutable int titleViewportTop_ = -1;
};

class WriteSurface : public cwb::FullscreenWidget {
 public:
  WriteSurface(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
               QWidget* parent)
      : cwb::FullscreenWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      rpc_ = ctx_->serverRpcPort();
      id_ = ctx_->authorIdentity();  // the one key, which owns the name zone
      authorName_ = ctx_->authorName();
      authorHandle_ = ctx_->authorHandle();  // the /f/<handle>/ zone owner
    }
    if (auto it = p.params.find(QStringLiteral("host")); it != p.params.end())
      host_ = it->second.toStdString();
    if (auto it = p.params.find(QStringLiteral("rpcport")); it != p.params.end())
      rpc_ = static_cast<quint16>(it->second.toUShort());
    // The service resolved the authenticated keyname; use its exact zone
    // component, never re-normalize a display string here.
    if (auto it = p.params.find(QStringLiteral("authorhandle"));
        it != p.params.end() && !it->second.isEmpty())
      authorHandle_ = it->second;
    if (auto it = p.params.find(QStringLiteral("approot")); it != p.params.end())
      appRoot_ = it->second;
    if (auto it = p.params.find(QStringLiteral("appsource"));
        it != p.params.end())
      appSource_ = it->second;  // the app's /s/ source, for stable app links
    draftKey_ = QStringLiteral("%1:%2|%3|%4")
                    .arg(QString::fromStdString(host_))
                    .arg(rpc_)
                    .arg(authorHandle_, appRoot_);

    QString product;
    if (auto it = p.params.find(QStringLiteral("appname")); it != p.params.end())
      product = it->second;
    if (product.isEmpty())
      if (auto it = p.params.find(QStringLiteral("approot"));
          it != p.params.end()) {
        product = it->second;
        if (!product.isEmpty()) product[0] = product[0].toUpper();
      }
    if (product.isEmpty()) product = QStringLiteral("Write");
    productName_ = product;
    if (auto it = p.params.find(QStringLiteral("home")); it != p.params.end())
      appHome_ = it->second;
    if (appHome_.isEmpty() && ctx_) {
      const QString current = ctx_->currentAddress();
      const int scheme = current.indexOf(QLatin1String("://"));
      const int path = scheme < 0 ? -1 : current.indexOf('/', scheme + 3);
      if (path >= 0) appHome_ = current.left(path + 1);
    }
    QString accent = QStringLiteral("#1a8917");
    if (auto it = p.params.find(QStringLiteral("accent")); it != p.params.end())
      accent = it->second;
    QColor accentColor(accent);
    if (!accentColor.isValid()) accentColor = QColor(QStringLiteral("#1a8917"));
    const QString accentHover = accentColor.darker(112).name();
    const QString accentPressed = accentColor.darker(138).name();

    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QWidget{background:#ffffff}"));
    auto* page = new QVBoxLayout(this);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("cwbWriteHeader"));
    constexpr int kHeaderHeight = 59;
    header->setFixedHeight(kHeaderHeight);
    header->setStyleSheet(QStringLiteral(
        "QFrame#cwbWriteHeader{background:#ffffff;border:none;"
        "border-bottom:1px solid #ececeb}"));
    auto* top = new QHBoxLayout(header);
    top->setContentsMargins(28, 12, 28, 12);
    auto* brand = new QPushButton(product, header);
    brand->setObjectName(QStringLiteral("cwbWriteBrand"));
    brand->setFlat(true);
    brand->setCursor(appHome_.isEmpty() ? Qt::ArrowCursor
                                        : Qt::PointingHandCursor);
    brand->setStyleSheet(
        QStringLiteral("QPushButton{font-family:serif;font-size:20px;"
                       "font-weight:700;color:#242424;background:transparent;"
                       "border:none;padding:0;margin:0;text-align:left}"
                       "QPushButton:hover{color:#242424}"
                       "QPushButton:pressed{color:#6b6b6b;padding-left:1px;"
                       "padding-top:1px}"
                       "QPushButton:focus{outline:none}"));
    if (!appHome_.isEmpty())
      connect(brand, &QPushButton::clicked, this, [this] {
        if (ctx_ && !appHome_.isEmpty()) ctx_->navigateTo(appHome_);
      });
    else
      brand->setEnabled(false);
    top->addWidget(brand);
    auto* draft = new QLabel(tr("Draft"), header);
    draft->setStyleSheet(QStringLiteral(
        "font-family:sans-serif;font-size:12px;color:#9c9c9a"));
    top->addSpacing(10);
    top->addWidget(draft);
    top->addStretch(1);

    words_ = new QLabel(header);
    words_->setStyleSheet(
        QStringLiteral("color:#9c9c9a;font-family:sans-serif;font-size:12px"));
    top->addWidget(words_);
    status_ = new QLabel(header);
    status_->setStyleSheet(
        QStringLiteral("color:#6b6b6b;font-family:sans-serif;font-size:12px"));
    status_->setTextFormat(Qt::RichText);
    status_->setOpenExternalLinks(false);
    connect(status_, &QLabel::linkActivated, this, [this](const QString& url) {
      if (ctx_) ctx_->navigateTo(url);
    });
    top->addSpacing(12);
    top->addWidget(status_);
    publish_ = new QPushButton(tr("Publish"), header);
    publish_->setObjectName(QStringLiteral("cwbWritePublish"));
    publish_->setCursor(Qt::PointingHandCursor);
    publish_->setStyleSheet(
        QStringLiteral(
            "QPushButton{background:%1;color:#fff;border:none;border-radius:17px;"
            "padding:8px 20px;font-family:sans-serif;font-size:13px}"
            "QPushButton:hover{background:%2}"
            "QPushButton:pressed{background:%3;padding:9px 19px 7px 21px}"
            "QPushButton:disabled{background:#c6e1c5;color:#ffffff}")
            .arg(accent, accentHover, accentPressed));
    top->addSpacing(14);
    top->addWidget(publish_);
    // Viewport chrome: the host pins it; the spacer reserves its height in
    // document flow.
    setViewportChrome(header);
    page->addSpacing(kHeaderHeight);

    auto* shell = new QHBoxLayout;
    shell->setContentsMargins(0, 0, 0, 0);
    auto* canvas = new QWidget(this);
    canvas->setObjectName(QStringLiteral("cwbWriteCanvas"));
    canvas->setMaximumWidth(756);  // 700px prose + 2 * 28px editor padding
    canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    canvas->setStyleSheet(QStringLiteral(
        "QWidget#cwbWriteCanvas{background:#fdfcf9;border-left:1px solid "
        "#f1efe9;border-right:1px solid #f1efe9}"));
    shell->addStretch(1);
    shell->addWidget(canvas, 6, Qt::AlignTop);
    shell->addStretch(1);
    page->addLayout(shell);
    // Spare height goes below the canvas, never around it (document flow).
    page->addStretch(1);
    auto* col = new QVBoxLayout(canvas);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    QFont serif(QStringLiteral("Charter"));
    serif.setFamilies({QStringLiteral("Charter"),
                       QStringLiteral("Bitstream Charter"),
                       QStringLiteral("Georgia"), QStringLiteral("Noto Serif"),
                       QStringLiteral("Liberation Serif")});

    QFont tf = serif;
    tf.setPixelSize(34);  // the article's own scale: writer == reader
    tf.setBold(true);
    QFont bf = serif;
    bf.setPixelSize(21);  // article body size exactly
    editor_ = new WriteEditor(tf, bf, canvas);
    editor_->setObjectName(QStringLiteral("cwbWriteEditor"));
    editor_->setCursor(Qt::IBeamCursor);
    editor_->setStyleSheet(QStringLiteral(
        "QTextEdit{border:none;background:#fdfcf9;color:#242424;"
        "padding:48px 28px 80px}"));
    editor_->setMinimumHeight(440);
    col->addWidget(editor_);

    // Resizing reflows the document and resizes the fullscreen page.
    editor_->widthChanged = [this] { scheduleEditorResize(); };

    draftTimer_ = new QTimer(this);
    draftTimer_->setSingleShot(true);
    draftTimer_->setInterval(350);
    connect(draftTimer_, &QTimer::timeout, this, [this] {
      if (!draftKey_.isEmpty())
        cwb::draftSave(draftKey_, editor_->title(), editor_->body());
    });

    connect(editor_, &QTextEdit::textChanged, this, [this] {
      editor_->applyBlockStyles();
      const bool hasContent = !editor_->title().isEmpty() ||
                              !editor_->body().isEmpty();
      const auto n =
          editor_->body().split(QRegularExpression("\\s+"),
                                Qt::SkipEmptyParts).size();
      words_->setText(n ? tr("%1 words").arg(n) : QString());
      // Dirty flag; the window confirms before navigation wipes the page.
      setProperty("cwbUnsaved", hasContent);
      publish_->setEnabled(hasContent && !publishing_);
      draftTimer_->start();
      scheduleEditorResize();
    });
    connect(editor_, &QTextEdit::cursorPositionChanged, this,
            [this] { scheduleCaretReveal(); });
    // Prefill after the counter is wired.
    QString initialTitle, initialBody;
    if (auto it = p.params.find(QStringLiteral("title")); it != p.params.end())
      initialTitle = it->second;
    if (auto it = p.params.find(QStringLiteral("body")); it != p.params.end())
      initialBody = it->second;
    if (initialTitle.isEmpty() && initialBody.isEmpty())
      cwb::draftLoad(draftKey_, initialTitle, initialBody);
    editor_->setParts(initialTitle, initialBody);
    publish_->setEnabled(!initialTitle.isEmpty() || !initialBody.isEmpty());
    if (auto it = p.params.find(QStringLiteral("name")); it != p.params.end())
      slugOverride_ = it->second.toStdString();
    cwbw::armConfirm(
        publish_, [this] { return tr("Publish now?"); },
        [this] { doPublish(); });
    scheduleEditorResize();
  }

  QSize sizeHint() const override {
    QSize hint = QWidget::sizeHint();
    hint.setHeight(std::max(hint.height(), 520));
    return hint;
  }

 private:
  void scheduleEditorResize() {
    if (editorResizePending_) return;
    editorResizePending_ = true;
    QTimer::singleShot(0, this, [this] {
      editorResizePending_ = false;
      resizeEditorToDocument();
    });
  }

  QScrollArea* outerScrollArea() const {
    for (QWidget* p = parentWidget(); p; p = p->parentWidget())
      if (auto* outer = qobject_cast<QScrollArea*>(p)) return outer;
    return nullptr;
  }

  void scheduleCaretReveal() {
    if (caretRevealPending_) return;
    caretRevealPending_ = true;
    QTimer::singleShot(0, this, [this] {
      caretRevealPending_ = false;
      revealCaret();
    });
  }

  void revealCaret() {
    auto* outer = outerScrollArea();
    if (!outer || !editor_->hasFocus()) return;
    editor_->verticalScrollBar()->setValue(0);
    const QRect caret = editor_->cursorRect();
    const QPoint caretTop =
        editor_->viewport()->mapTo(outer->viewport(), caret.topLeft());
    const int caretBottom = caretTop.y() + caret.height();
    const int chromeBottom = viewportChrome() ? viewportChrome()->height() : 0;
    constexpr int kCaretMargin = 36;
    auto* scroll = outer->verticalScrollBar();
    if (caretBottom > outer->viewport()->height() - kCaretMargin)
      scroll->setValue(scroll->value() + caretBottom -
                       (outer->viewport()->height() - kCaretMargin));
    else if (caretTop.y() < chromeBottom + kCaretMargin)
      scroll->setValue(scroll->value() + caretTop.y() -
                       (chromeBottom + kCaretMargin));
  }

  void resizeEditorToDocument() {
    // Preserve the browser scroll position while geometry catches up.
    QScrollArea* outer = outerScrollArea();
    const int outerY = outer ? outer->verticalScrollBar()->value() : 0;
    editor_->document()->setTextWidth(
        std::max(1, editor_->viewport()->width()));
    const int editorChrome = editor_->height() - editor_->viewport()->height();
    const int wanted = std::max(
        440, static_cast<int>(std::ceil(editor_->document()->size().height())) +
                 editorChrome);
    if (editor_->height() != wanted) editor_->setFixedHeight(wanted);
    const int editorTop = editor_->mapTo(this, QPoint(0, 0)).y();
    setMinimumHeight(std::max(520, editorTop + wanted + 80));
    editor_->verticalScrollBar()->setValue(0);
    editor_->horizontalScrollBar()->setValue(0);
    updateGeometry();
    emit contentSizeChanged();
    if (outer) {
      outer->verticalScrollBar()->setValue(outerY);
      revealCaret();
    }
  }

  void doPublish() {
    const QString title = editor_->title();
    const QString body = editor_->body();
    if (title.isEmpty() && body.isEmpty()) {
      status_->setText(tr("write something first"));
      return;
    }
    if (host_.empty() || rpc_ == 0) {
      status_->setText(tr("no server: browse to a file server first"));
      return;
    }
    if (authorHandle_.isEmpty()) {
      // No name yet: one-click hand-off to the account page (main = rpc - 1).
      const quint16 mainPort = (rpc_ > 1) ? static_cast<quint16>(rpc_ - 1) : 0;
      const QString acct =
          QStringLiteral("ces://%1:%2/account")
              .arg(QString::fromStdString(host_))
              .arg(mainPort);
      status_->setText(tr("Set your name first: "
                          "<a href=\"%1\">open your account</a> "
                          "(mine a credit or two there, then set a name).")
                           .arg(acct));
      return;
    }
    if (appRoot_.isEmpty()) {  // the application (page) failed to declare itself
      status_->setText(tr("this page did not say where to publish"));
      return;
    }
    publishing_ = true;
    publish_->setEnabled(false);
    status_->setText(tr("publishing…"));
    const QString t = title.isEmpty() ? tr("Untitled") : title;
    const std::string hex = id_.getPublicKeyHexStr();
    const QString zonePrefix = QStringLiteral("/f/%1/%2/")
                                   .arg(authorHandle_, appRoot_);
    // kebab title + content hash; an edited story keeps its name (worktree
    // matches by title). The harness `name` param overrides.
    QString fileName;
    if (!slugOverride_.empty()) {
      fileName = QString::fromStdString(slugOverride_) +
                 QStringLiteral(".html");
    } else {
      const QString existing = cwb::workFindByTitle(zonePrefix, t);
      fileName = existing.isEmpty()
                     ? cwb::storyFileName(
                           t, t.toUtf8() + "\n\n" + body.toUtf8())
                     : existing.section('/', -1);
    }
    const std::string path = (zonePrefix + fileName).toStdString();
    // Semantic paragraphs, never viewport-dependent visual line breaks.
    const QString titleFlow = editor_->titleFlowHtml();
    const QString bodyFlow = editor_->bodyFlowHtml();
    // The app source gives every artifact a stable link into the live app.
    const QString appSource =
        appSource_.isEmpty() ? QStringLiteral("/s/%1.lua").arg(appRoot_)
                             : appSource_;
    const QString authorPage = QStringLiteral("compute://%1:%2%3/by/%4")
                                   .arg(QString::fromStdString(host_))
                                   .arg(rpc_)
                                   .arg(appSource, authorHandle_);
    const std::string html =
        articleHtml(titleFlow, bodyFlow, hex, authorName_, productName_,
                    appHome_, path, t, appSource, authorPage);
    const QString zonePath = QString::fromStdString(path);
    const QByteArray source = t.toUtf8() + "\n\n" + body.toUtf8();
    if (!cwb::workSave(zonePath,
                       QByteArray(html.data(), static_cast<int>(html.size())),
                       source, t)) {
      publishing_ = false;
      publish_->setEnabled(true);
      status_->setText(tr("could not save the local primary copy"));
      return;
    }
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    const QString storyTitle = t;
    const QString aname = authorName_;
    cwb::WidgetContext* ctx = ctx_;
    QPointer<WriteSurface> self(this);
    std::thread([self, ctx, host, rpc, id, path, html, storyTitle, aname]() {
      QString msg;
      QString liveUrl;
      try {
        ces::CesFileClient fc;
        uint8_t rc = fc.connect(host, rpc, id);
        if (rc != ces::CES_OK) {
          msg = QStringLiteral("connect: %1")
                    .arg(QString::fromUtf8(ces::errorString(rc)));
        } else {
          ces::Bytes bytes(html.begin(), html.end());
          // Deposit ladder: step down on INSUFFICIENT_BALANCE; the smallest
          // rung still covers the 15-min upfront rent minimum.
          static constexpr uint64_t kLadder[] = {5'000'000, 1'000'000, 200'000,
                                                 50'000, 10'000};
          uint64_t fbal = 0, cost = 0, fed = 0;
          rc = ces::CES_ERROR_INSUFFICIENT_BALANCE;
          for (uint64_t dep : kLadder) {
            rc = fc.create(path, bytes.size(), 0, dep, fbal, cost);
            if (rc != ces::CES_ERROR_INSUFFICIENT_BALANCE) { fed = dep; break; }
          }
          if (rc == ces::CES_ERROR_FILE_EXISTS) {
            ces::CesFileClient::StatInfo si{};
            rc = fc.stat(path, si);
            if (rc == ces::CES_OK && si.size != bytes.size()) {
              uint64_t newSize = 0;
              rc = fc.resize(path, bytes.size(), newSize);
            }
          }
          if (rc == ces::CES_OK) {
            uint64_t wb = 0;
            rc = fc.write(path, 0, bytes, wb);
            if (rc == ces::CES_OK) {
              liveUrl = QStringLiteral("file://%1:%2%3")
                            .arg(QString::fromStdString(host))
                            .arg(rpc)
                            .arg(QString::fromStdString(path));
              msg = QStringLiteral("Live. (story fund: %1)")
                        .arg(fed ? cwb::creditsText(static_cast<qint64>(fed)) +
                                       QStringLiteral(" credits")
                                 : QStringLiteral("existing"));
              // Record this server as a remote of the local primary.
              try {
                cwb::workNoteRemote(QString::fromStdString(path),
                                    QStringLiteral("%1:%2")
                                            .arg(QString::fromStdString(host))
                                            .arg(rpc));
              } catch (...) {
                // the work tree must never block a publish
              }
              // Announce to Vellum if this server runs it. Best-effort; the
              // announcement is a hint, the file store is the truth.
              try {
                ces::CesComputeClient cc;
                if (cc.connect(host, rpc, id) == ces::CES_OK) {
                  std::vector<ces::CesComputeClient::InstanceInfo> vi;
                  if (cc.instances("/s/vellum.lua", vi) == ces::CES_OK &&
                      !vi.empty()) {
                    QString ttl = storyTitle;
                    ttl.replace(QLatin1Char('|'), QLatin1Char(' '));
                    QString an = aname;
                    an.replace(QLatin1Char('|'), QLatin1Char(' '));
                    const std::string msg =
                        "published|" + path + "|" + ttl.toUtf8().toStdString() +
                        "|" + an.toUtf8().toStdString() + "\n";
                    std::string resp;
                    uint8_t as = 0;
                    cwb::cesLuaFetch(host, rpc, vi.front().pid, id, msg, resp,
                                     as);
                  }
                  cc.disconnect();
                }
              } catch (...) {
                // the feed is optional; the story is already live
              }
            } else {
              msg = QStringLiteral("write: %1")
                        .arg(QString::fromUtf8(ces::errorString(rc)));
            }
          } else {
            msg = QStringLiteral("create: %1")
                      .arg(QString::fromUtf8(ces::errorString(rc)));
          }
          fc.disconnect();
        }
      } catch (...) {
        msg = QStringLiteral("publish failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, ctx, msg, liveUrl]() {
            if (!self) return;
            self->status_->setText(msg);
            self->publishing_ = false;
            self->publish_->setEnabled(!self->editor_->title().isEmpty() ||
                                       !self->editor_->body().isEmpty());
            if (!liveUrl.isEmpty() && ctx) {
              // Clear dirty BEFORE navigating or the beforeunload guard fires.
              self->setProperty("cwbUnsaved", false);
              cwb::draftRemove(self->draftKey_);
              QTimer::singleShot(700, qApp, [ctx, liveUrl]() {
                ctx->navigateTo(liveUrl);
              });
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 rpc_ = 0;
  ces::KeyPair id_;
  QString authorName_;
  QString authorHandle_;
  QString appRoot_;  // the application's directory: set by the PAGE, never a
                     // browser default (the browser has no opinion on apps)
  QString appSource_;  // the app's /s/ source path, for stable app links
  QString productName_;
  QString appHome_;
  QString draftKey_;
  std::string slugOverride_;
  WriteEditor* editor_ = nullptr;
  bool editorResizePending_ = false;
  bool caretRevealPending_ = false;
  bool publishing_ = false;
  QTimer* draftTimer_ = nullptr;
  QLabel* words_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* publish_ = nullptr;
};

QWidget* makeWriteSurface(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                          QWidget* parent) {
  return new WriteSurface(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_write("application/x-cwb-write",
                                     &makeWriteSurface);

}  // namespace
