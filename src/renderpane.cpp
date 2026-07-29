#include <QDebug>
#include "renderpane.h"
#include "widget.h"

#include <QChar>
#include <QContextMenuEvent>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <litehtml/html_tag.h>
#include <litehtml/render_image.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
QColor toQColor(const litehtml::web_color& c) {
  return QColor(c.red, c.green, c.blue, c.alpha);
}

// User-agent link styling under the page's own CSS. :active keeps the
// author's link color (an anchor may be a styled button); underline gives
// press feedback without positioning the inline box (litehtml can drop a
// positioned inline box for a frame).
const char* kUaUserCss =
    "a:link { color: #1a0dab; }\n"
    "a:hover { text-decoration: underline; }\n"
    "a:active { text-decoration: underline; }\n"
    "button:active, input[type=button]:active, input[type=submit]:active { "
    "position:relative; left:1px; top:1px; }\n";

// A registered <object>/<embed> is a replaced box (width/height attrs,
// default 300x150); the native widget overlays it in placeWidgets(). <param>
// children stay in the tree but out of visual flow, so fallback text never
// draws behind the widget.
class EmbedElement : public litehtml::html_tag {
 public:
  explicit EmbedElement(const litehtml::document::ptr& doc)
      : litehtml::html_tag(doc) {
    m_css.set_display(litehtml::display_inline_block);
  }
  bool is_replaced() const override { return true; }
  void parse_attributes() override {
    w_ = parseDim(get_attr("width"), 300);
    h_ = parseDim(get_attr("height"), 150);
  }
  void get_content_size(litehtml::size& sz, litehtml::pixel_t) override {
    sz.width = litehtml::pixel_t(w_);
    sz.height = litehtml::pixel_t(h_);
  }
  void draw(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
            const litehtml::position* clip,
            const std::shared_ptr<litehtml::render_item>& ri) override {
    litehtml::html_tag::draw(hdc, x, y, clip, ri);  // background/border only
  }
  std::shared_ptr<litehtml::render_item> create_render_item(
      const std::shared_ptr<litehtml::render_item>& parent_ri) override {
    auto ri = std::make_shared<litehtml::render_item_image>(shared_from_this());
    ri->parent(parent_ri);
    return ri;
  }

 private:
  static int parseDim(const char* s, int dflt) {
    if (!s || !*s) return dflt;
    const int v = std::atoi(s);
    return v > 0 ? v : dflt;
  }
  int w_ = 300, h_ = 150;
};

// Collect the visible text runs (litehtml splits text into per-word nodes) in
// document order, each with its laid-out box, for find-in-page. Whitespace-only
// nodes are skipped; the caller joins runs with single spaces so a query can
// match across words.
struct TextRun {
  QString text;
  QRect rect;
  QString separatorBefore;  // DOM structure before this run: space or newline
};
// Depth-first search for the element carrying id="want" (fragment target).
litehtml::element::ptr findById(const litehtml::element::ptr& el,
                                const std::string& want) {
  if (!el) return {};
  if (const char* a = el->get_attr("id"))
    if (want == a) return el;
  for (const auto& c : el->children())
    if (auto r = findById(c, want)) return r;
  return {};
}

bool startsTextBlock(const char* tag) {
  if (!tag) return false;
  static const std::set<std::string> blocks = {
      "address", "article", "aside", "blockquote", "div", "dl", "dt",
      "dd",      "figcaption", "figure", "footer", "form", "h1", "h2",
      "h3",      "h4", "h5", "h6", "header", "li", "main", "nav",
      "ol",      "p", "pre", "section", "table", "tr", "ul"};
  return blocks.contains(tag);
}

void collectTextRuns(const litehtml::element::ptr& el, std::vector<TextRun>& out) {
  if (!el) return;
  if (el->is_text()) {
    std::string s;
    el->get_text(s);
    const QString qs = QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
    if (!qs.trimmed().isEmpty()) {
      const litehtml::position p = el->get_placement();
      const int rw = int(p.width), rh = int(p.height);
      // Skip text that isn't actually laid out on screen (e.g. the suppressed
      // fallback children of a replaced <object>): a 0-size box = not visible.
      if (rw > 0 && rh > 0)
        out.push_back({qs, QRect(int(p.x), int(p.y), rw, rh),
                       out.empty() ? QString() : QStringLiteral(" ")});
    }
    return;  // text nodes have no element children
  }
  const size_t firstRun = out.size();
  bool lineBreakPending = false;
  for (const auto& c : el->children()) {
    const char* childTag = c->get_tagName();
    if (childTag && std::strcmp(childTag, "br") == 0) {
      lineBreakPending = true;
      continue;
    }
    const size_t childFirst = out.size();
    collectTextRuns(c, out);
    if (lineBreakPending && childFirst < out.size()) {
      out[childFirst].separatorBefore = QStringLiteral("\n");
      lineBreakPending = false;
    }
  }
  if (firstRun < out.size() && firstRun > 0 && startsTextBlock(el->get_tagName()))
    out[firstRun].separatorBefore = QStringLiteral("\n");
}
}  // namespace

RenderPane::RenderPane(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setMouseTracking(true);  // hover cursor over links without a pressed button
  setFocusPolicy(Qt::StrongFocus);
}

void RenderPane::mousePressEvent(QMouseEvent* e) {
  if (!doc_ || e->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(e);
    return;
  }
  const auto x = litehtml::pixel_t(static_cast<int>(e->position().x()));
  const auto y = litehtml::pixel_t(static_cast<int>(e->position().y()));
  auto redraw = [this](const litehtml::position&) { update(); };
  setFocus(Qt::MouseFocusReason);
  selecting_ = true;
  selectionDragged_ = false;
  selectionPressPos_ = e->position().toPoint();
  const int hit = textPositionAt(selectionPressPos_);
  if (e->modifiers() & Qt::ShiftModifier)
    selFocus_ = hit;
  else
    selAnchor_ = selFocus_ = hit;
  update();
  if (doc_->on_lbutton_down(x, y, x, y, redraw)) update();
}

void RenderPane::mouseReleaseEvent(QMouseEvent* e) {
  if (!doc_ || e->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(e);
    return;
  }
  const auto x = litehtml::pixel_t(static_cast<int>(e->position().x()));
  const auto y = litehtml::pixel_t(static_cast<int>(e->position().y()));
  auto redraw = [this](const litehtml::position&) { update(); };
  selecting_ = false;
  // Always deliver mouse-up so litehtml clears :active, including after a text
  // selection drag.  Suppress only the resulting click callback: a drag must
  // never navigate even if it ends back over the anchor it began on.
  suppressLinkActivation_ = selectionDragged_;
  if (doc_->on_lbutton_up(x, y, x, y, redraw)) update();
  suppressLinkActivation_ = false;
}

void RenderPane::mouseMoveEvent(QMouseEvent* e) {
  if (!doc_) {
    QWidget::mouseMoveEvent(e);
    return;
  }
  const auto x = litehtml::pixel_t(static_cast<int>(e->position().x()));
  const auto y = litehtml::pixel_t(static_cast<int>(e->position().y()));
  if (selecting_ && (e->buttons() & Qt::LeftButton)) {
    if ((e->position().toPoint() - selectionPressPos_).manhattanLength() >=
        QApplication::startDragDistance())
      selectionDragged_ = true;
    selFocus_ = textPositionAt(e->position().toPoint());
    update();
  }
  auto redraw = [this](const litehtml::position&) { update(); };
  if (doc_->on_mouse_over(x, y, x, y, redraw)) update();
  // Report the anchor under the cursor (walk up from the hovered element).
  QString href;
  for (auto el = doc_->get_over_element(); el; el = el->parent()) {
    if (const char* h = el->get_attr("href")) {
      href = QString::fromUtf8(h);
      break;
    }
  }
  if (href != lastHoverHref_) {
    lastHoverHref_ = href;
    emit hoverLink(href);
  }
}

RenderPane::~RenderPane() = default;

void RenderPane::renderDoc(int width) {
  if (!doc_ || width <= 0) return;
  viewport_ = QSize(width, std::max(height(), 1));
  doc_->render(litehtml::pixel_t(width));
  docWidth_ = width;
  docHeight_ = static_cast<int>(std::ceil(static_cast<float>(doc_->height())));
}

void RenderPane::setHtml(const QString& html) {
  html_ = html;
  cssCache_.clear();
  imgCache_.clear();
  rebuild();
}

void RenderPane::rebuild() {
  wantedCss_.clear();
  wantedImg_.clear();
  const std::string s = html_.toStdString();
  std::string userCss = kUaUserCss;
  for (const auto& h : visitedHrefs_) {
    std::string esc;  // CSS-escape the href for the attribute selector
    for (char c : h) {
      if (c == '\\' || c == '"') esc.push_back('\\');
      esc.push_back(c);
    }
    userCss += "a[href=\"" + esc + "\"]{color:#551a8b !important}\n";
  }
  doc_ = litehtml::document::createFromString(s, this, litehtml::master_css,
                                              userCss);  // import_css runs here
  const int w = width() > 0 ? width() : viewport_.width();
  if (doc_) renderDoc(w);
  // A throwaway draw makes litehtml call load_image for every referenced image
  // now (synchronously), so wantedImages() is complete before the loader asks.
  if (doc_ && docWidth_ > 0 && docHeight_ > 0) {
    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter pp(&probe);
    paintRect_ = QRect(0, 0, docWidth_, docHeight_);
    litehtml::position clip(0, 0, litehtml::pixel_t(docWidth_),
                            litehtml::pixel_t(docHeight_));
    doc_->draw(reinterpret_cast<litehtml::uint_ptr>(&pp), 0, 0, &clip);
    pp.end();
  }
  setMinimumHeight(docHeight_);
  updateGeometry();
  placeWidgets();
  if (!findQuery_.isEmpty()) rebuildFindHits();
  update();
}

void RenderPane::clearWidgets() {
  for (QWidget* w : widgets_) w->deleteLater();
  widgets_.clear();
}

bool RenderPane::hasUnsavedContent() const {
  bool any = false;
  for (QWidget* w : widgets_) {
    const bool u = w && w->property("cwbUnsaved").toBool();
    qWarning().noquote() << "cwb-nav: hasUnsavedContent widget="
                         << (w ? w->metaObject()->className() : "null")
                         << "unsaved=" << u;
    if (u) any = true;
  }
  qWarning() << "cwb-nav: hasUnsavedContent widgets=" << int(widgets_.size())
             << "-> " << any;
  return any;
}

// Recursively collect <object>/<embed> elements from the litehtml tree.
static void collectEmbeds(const litehtml::element::ptr& el,
                          std::vector<litehtml::element::ptr>& out) {
  if (!el) return;
  const char* tag = el->get_tagName();
  if (tag && (std::strcmp(tag, "object") == 0 || std::strcmp(tag, "embed") == 0))
    out.push_back(el);
  for (const auto& c : el->children()) collectEmbeds(c, out);
}

void RenderPane::placeWidgets() {
  if (!doc_ || !doc_->root()) {
    clearWidgets();
    widgetsHtml_.clear();
    return;
  }
  // The registered <object>/<embed> boxes, in tree order.
  std::vector<litehtml::element::ptr> embeds;
  collectEmbeds(doc_->root(), embeds);
  std::vector<std::pair<litehtml::element::ptr, cwb::WidgetParams>> reg;
  for (const auto& el : embeds) {
    cwb::WidgetParams p;
    if (const char* t = el->get_attr("type")) p.type = QString::fromUtf8(t);
    if (const char* d = el->get_attr("data")) p.data = QString::fromUtf8(d);
    if (p.data.isEmpty())
      if (const char* s = el->get_attr("src")) p.data = QString::fromUtf8(s);
    for (const auto& c : el->children()) {
      const char* ctag = c->get_tagName();
      if (!ctag || std::strcmp(ctag, "param") != 0) continue;
      const char* n = c->get_attr("name");
      const char* v = c->get_attr("value");
      if (n && v) p.params[QString::fromUtf8(n)] = QString::fromUtf8(v);
    }
    // Unknown type -> leave the element's fallback content (real <object>
    // semantics), draw nothing native.
    if (cwb::WidgetRegistry::instance().has(p.type)) reg.push_back({el, p});
  }

  auto boxGeom = [](const litehtml::element::ptr& el, QWidget* w) {
    const litehtml::position box = el->get_placement();
    const int boxW = int(box.width), boxH = int(box.height);
    const int bw = boxW > 0 ? boxW : w->sizeHint().width();
    const int bh = boxH > 0 ? boxH : w->sizeHint().height();
    return QRect(int(box.x), int(box.y), bw, bh);
  };

  // Same page re-laid-out: REUSE live widgets, only move them. A recreate
  // wipes typed text and yanks focus. Recreate only when the page changed.
  if (html_ == widgetsHtml_ && widgets_.size() == reg.size()) {
    for (size_t i = 0; i < reg.size(); ++i)
      widgets_[i]->setGeometry(boxGeom(reg[i].first, widgets_[i]));
    return;
  }

  clearWidgets();
  for (const auto& [el, p] : reg) {
    QWidget* w = cwb::WidgetRegistry::instance().create(p, widgetCtx_, this);
    if (!w) continue;
    w->setGeometry(boxGeom(el, w));
    w->show();
    widgets_.push_back(w);
  }
  widgetsHtml_ = html_;
}

QStringList RenderPane::wantedCss() const {
  QStringList out;
  for (const auto& u : wantedCss_) out << QString::fromStdString(u);
  return out;
}

QStringList RenderPane::wantedImages() const {
  QStringList out;
  for (const auto& u : wantedImg_) out << QString::fromStdString(u);
  return out;
}

void RenderPane::provideCss(const QString& url, const QString& text) {
  cssCache_[url.toStdString()] = text.toStdString();
  rebuild();  // re-parse so the new stylesheet applies; relayout
}

void RenderPane::provideImage(const QString& url, const QImage& img) {
  imgCache_[url.toStdString()] = img;
  rebuild();  // relayout now that the image has real dimensions
}

void RenderPane::setVisitedHrefs(const QStringList& hrefs) {
  visitedHrefs_.clear();
  for (const QString& h : hrefs) visitedHrefs_.insert(h.toStdString());
  rebuild();  // re-apply with the visited rules
}

QImage RenderPane::renderToImage(int width) {
  if (width <= 0) width = 900;
  if (!doc_) return QImage();
  renderDoc(width);
  const int h = std::max(docHeight_, 1);
  QImage img(width, h, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  paintRect_ = QRect(0, 0, width, h);
  litehtml::position clip(0, 0, litehtml::pixel_t(width), litehtml::pixel_t(h));
  doc_->draw(reinterpret_cast<litehtml::uint_ptr>(&p), 0, 0, &clip);

  // Read-only browser selection: translucent system-blue over exact portions
  // of the laid-out text-run boxes. No cursor is ever painted.
  if (hasSelection()) {
    const QColor selection(46, 115, 252, 105);
    for (const QRectF& r : selectionRects()) p.fillRect(r, selection);
  }
  p.end();
  return img;
}

void RenderPane::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), Qt::white);
  if (!doc_) return;
  if (docWidth_ != width()) {
    renderDoc(width());
    placeWidgets();
    if (!findQuery_.isEmpty()) rebuildFindHits();
  }
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  paintRect_ = rect();
  litehtml::position clip(0, 0, litehtml::pixel_t(width()), litehtml::pixel_t(height()));
  doc_->draw(reinterpret_cast<litehtml::uint_ptr>(&p), 0, 0, &clip);

  // Read-only browser selection in the live QWidget paint path. The selection
  // model is independent of find-in-page and never paints an editable caret.
  if (hasSelection()) {
    const QColor selection(46, 115, 252, 105);
    for (const QRectF& r : selectionRects()) p.fillRect(r, selection);
  }

  // Find-in-page highlights, over the drawn text: all matches light, the
  // selected one strong (and last, so it wins any overlap).
  if (!findHits_.empty()) {
    const QColor light(255, 230, 0, 110);
    const QColor strong(255, 150, 0, 160);
    for (int i = 0; i < static_cast<int>(findHits_.size()); ++i)
      if (i != findCur_)
        for (const QRect& r : findHits_[i].rects) p.fillRect(r, light);
    if (findCur_ >= 0 && findCur_ < static_cast<int>(findHits_.size()))
      for (const QRect& r : findHits_[findCur_].rects) p.fillRect(r, strong);
  }
}

// (relayout on width change re-runs placeWidgets via resizeEvent/paintEvent)

void RenderPane::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  if (doc_ && width() > 0 && width() != docWidth_) {
    renderDoc(width());
    setMinimumHeight(docHeight_);
    updateGeometry();
    placeWidgets();
    if (!findQuery_.isEmpty()) rebuildFindHits();
  }
}

void RenderPane::contextMenuEvent(QContextMenuEvent* e) {
  // The anchor under the cursor (updated by hover) decides link vs page menu.
  emit contextMenuRequested(lastHoverHref_, e->globalPos());
}

int RenderPane::documentTextLength() const {
  if (!doc_ || !doc_->root()) return 0;
  std::vector<TextRun> runs;
  collectTextRuns(doc_->root(), runs);
  int n = 0;
  for (const TextRun& run : runs)
    n += run.separatorBefore.size() + run.text.size();
  return n;
}

int RenderPane::textPositionAt(const QPoint& point) const {
  if (!doc_ || !doc_->root()) return 0;
  std::vector<TextRun> runs;
  collectTextRuns(doc_->root(), runs);
  if (runs.empty()) return 0;
  int offset = 0;
  int best = 0;
  qreal bestDistance = std::numeric_limits<qreal>::max();
  for (size_t i = 0; i < runs.size(); ++i) {
    offset += runs[i].separatorBefore.size();
    const QRect& r = runs[i].rect;
    const qreal dx = point.x() < r.left() ? r.left() - point.x()
                    : point.x() > r.right() ? point.x() - r.right() : 0;
    const qreal dy = point.y() < r.top() ? r.top() - point.y()
                    : point.y() > r.bottom() ? point.y() - r.bottom() : 0;
    const qreal distance = dy * 10000 + dx;
    if (distance < bestDistance) {
      bestDistance = distance;
      const qreal fraction = r.width() > 0
                                 ? std::clamp((point.x() - r.left()) / qreal(r.width()),
                                              qreal(0), qreal(1))
                                 : 0;
      best = offset + qRound(fraction * runs[i].text.size());
    }
    offset += runs[i].text.size();
  }
  return best;
}

QString RenderPane::selectedText() const {
  if (!hasSelection() || !doc_ || !doc_->root()) return {};
  std::vector<TextRun> runs;
  collectTextRuns(doc_->root(), runs);
  QString flat;
  for (const TextRun& run : runs) {
    flat += run.separatorBefore;
    flat += run.text;
  }
  const int flatSize = static_cast<int>(flat.size());
  const int a = std::clamp(std::min(selAnchor_, selFocus_), 0, flatSize);
  const int b = std::clamp(std::max(selAnchor_, selFocus_), 0, flatSize);
  return flat.mid(a, b - a);
}

std::vector<QRectF> RenderPane::selectionRects() const {
  std::vector<QRectF> out;
  if (!hasSelection() || !doc_ || !doc_->root()) return out;
  std::vector<TextRun> runs;
  collectTextRuns(doc_->root(), runs);
  const int lo = std::min(selAnchor_, selFocus_);
  const int hi = std::max(selAnchor_, selFocus_);
  int offset = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    offset += runs[i].separatorBefore.size();
    const int runBegin = offset;
    const int runEnd = offset + runs[i].text.size();
    const int a = std::max(lo, runBegin);
    const int b = std::min(hi, runEnd);
    if (a < b) {
      QRectF r = runs[i].rect;
      const qreal charWidth =
          r.width() / std::max(1, static_cast<int>(runs[i].text.size()));
      r.setLeft(r.left() + (a - runBegin) * charWidth);
      r.setRight(runs[i].rect.left() + (b - runBegin) * charWidth);
      out.push_back(r);
    }
    offset = runEnd;
  }
  return out;
}

void RenderPane::copySelection() const {
  const QString text = selectedText();
  if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}

void RenderPane::selectAllText() {
  selAnchor_ = 0;
  selFocus_ = documentTextLength();
  update();
}

void RenderPane::clearSelection() {
  selAnchor_ = selFocus_ = 0;
  update();
}

void RenderPane::keyPressEvent(QKeyEvent* e) {
  if (e->matches(QKeySequence::Copy)) {
    copySelection();
    e->accept();
    return;
  }
  if (e->matches(QKeySequence::SelectAll)) {
    selectAllText();
    e->accept();
    return;
  }
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  int delta = 0;
  if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) delta = -1;
  if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Down) delta = 1;
  if (delta && shift) {
    selFocus_ = std::clamp(selFocus_ + delta, 0, documentTextLength());
    update();
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    clearSelection();
    e->accept();
    return;
  }
  QWidget::keyPressEvent(e);
}

QSize RenderPane::sizeHint() const {
  return QSize(docWidth_ > 0 ? docWidth_ : viewport_.width(),
               docHeight_ > 0 ? docHeight_ : viewport_.height());
}

litehtml::uint_ptr RenderPane::create_font(const litehtml::font_description& d,
                                           const litehtml::document*,
                                           litehtml::font_metrics* fm) {
  auto* f = new QFont();
  // The full CSS family stack, not just the head: concrete names go to
  // setFamilies (Qt tries each in turn); the first generic keyword becomes the
  // style hint that steers substitution when none of the names exist.
  QStringList fams;
  bool hinted = false;
  for (QString part : QString::fromStdString(d.family).split(',')) {
    part = part.trimmed();
    part.remove('"');
    part.remove('\'');
    if (part.isEmpty()) continue;
    const QString low = part.toLower();
    QFont::StyleHint hint = QFont::AnyStyle;
    if (low == QLatin1String("serif")) hint = QFont::Serif;
    else if (low == QLatin1String("sans-serif")) hint = QFont::SansSerif;
    else if (low == QLatin1String("monospace")) hint = QFont::Monospace;
    else if (low == QLatin1String("cursive")) hint = QFont::Cursive;
    else if (low == QLatin1String("fantasy")) hint = QFont::Fantasy;
    if (hint != QFont::AnyStyle) {
      if (!hinted) {
        f->setStyleHint(hint);
        hinted = true;
      }
      continue;  // generic keyword, not a real family name
    }
    fams << part;
  }
  if (!fams.isEmpty()) f->setFamilies(fams);
  int px = static_cast<int>(std::lround(static_cast<double>(d.size) * zoom_));
  if (px < 1) px = 1;
  f->setPixelSize(px);
  int w = d.weight;
  if (w < 1) w = 400;
  if (w > 1000) w = 1000;
  f->setWeight(static_cast<QFont::Weight>(w));
  if (d.style == litehtml::font_style_italic) f->setItalic(true);
  if (d.decoration_line & litehtml::text_decoration_line_underline) f->setUnderline(true);
  if (d.decoration_line & litehtml::text_decoration_line_line_through) f->setStrikeOut(true);
  if (fm) {
    QFontMetricsF qm(*f);
    fm->font_size = px;
    fm->ascent = static_cast<float>(qm.ascent());
    fm->descent = static_cast<float>(qm.descent());
    fm->height = static_cast<float>(qm.height());
    fm->x_height = static_cast<float>(qm.xHeight());
    fm->ch_width = static_cast<float>(qm.horizontalAdvance(QChar('0')));
    fm->draw_spaces = true;
    fm->sub_shift = static_cast<float>(qm.descent()) / 2.0f;
    fm->super_shift = static_cast<float>(qm.ascent()) / 2.0f;
  }
  return reinterpret_cast<litehtml::uint_ptr>(f);
}

void RenderPane::delete_font(litehtml::uint_ptr hFont) {
  delete reinterpret_cast<QFont*>(hFont);
}

litehtml::pixel_t RenderPane::text_width(const char* text, litehtml::uint_ptr hFont) {
  auto* f = reinterpret_cast<QFont*>(hFont);
  QFontMetricsF qm(*f);
  return litehtml::pixel_t(static_cast<float>(qm.horizontalAdvance(QString::fromUtf8(text))));
}

void RenderPane::draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
                           litehtml::web_color color, const litehtml::position& pos) {
  if (!text || !*text) return;
  auto* p = reinterpret_cast<QPainter*>(hdc);
  auto* f = reinterpret_cast<QFont*>(hFont);
  p->setFont(*f);
  p->setPen(toQColor(color));
  QFontMetricsF qm(*f);
  p->drawText(QPointF(static_cast<float>(pos.x), static_cast<float>(pos.y) + qm.ascent()),
              QString::fromUtf8(text));
}

litehtml::pixel_t RenderPane::pt_to_px(float pt) const {
  return litehtml::pixel_t(pt * 96.0f / 72.0f);
}

litehtml::pixel_t RenderPane::get_default_font_size() const { return litehtml::pixel_t(16); }

const char* RenderPane::get_default_font_name() const { return "sans-serif"; }

void RenderPane::setZoom(double z) {
  z = std::clamp(z, 0.5, 3.0);
  if (z == zoom_) return;
  zoom_ = z;
  if (doc_) rebuild();  // re-parse + re-lay-out every font at the new scale
}

void RenderPane::rebuildFindHits() {
  findHits_.clear();
  if (findQuery_.isEmpty()) {
    findCur_ = -1;
    return;
  }
  if (!doc_ || !doc_->root()) return;
  std::vector<TextRun> runs;
  collectTextRuns(doc_->root(), runs);
  // Concatenate runs with single spaces, remembering each run's span, so a query
  // can match within a word or across adjacent words.
  QString concat;
  std::vector<int> beg(runs.size()), end(runs.size());
  for (size_t i = 0; i < runs.size(); ++i) {
    concat += runs[i].separatorBefore;
    beg[i] = concat.size();
    concat += runs[i].text;
    end[i] = concat.size();
  }
  const QString hay = concat.toLower();
  const QString needle = findQuery_.toLower();
  for (int from = 0;;) {
    const int idx = hay.indexOf(needle, from);
    if (idx < 0) break;
    const int mEnd = idx + needle.size();
    FindHit hit;
    for (size_t i = 0; i < runs.size(); ++i)
      if (beg[i] < mEnd && end[i] > idx) hit.rects.push_back(runs[i].rect);
    if (!hit.rects.empty()) findHits_.push_back(std::move(hit));
    from = idx + static_cast<int>(needle.size());  // needle is non-empty here
  }
  if (findHits_.empty())
    findCur_ = -1;
  else if (findCur_ < 0 || findCur_ >= static_cast<int>(findHits_.size()))
    findCur_ = 0;
}

int RenderPane::findText(const QString& query) {
  findQuery_ = query;
  rebuildFindHits();
  findCur_ = findHits_.empty() ? -1 : 0;
  update();
  if (findCur_ >= 0 && !findHits_[findCur_].rects.empty())
    emit findScrollTo(findHits_[findCur_].rects.front());
  return static_cast<int>(findHits_.size());
}

void RenderPane::findNext() {
  if (findHits_.empty()) return;
  findCur_ = (findCur_ + 1) % static_cast<int>(findHits_.size());
  update();
  if (!findHits_[findCur_].rects.empty())
    emit findScrollTo(findHits_[findCur_].rects.front());
}

void RenderPane::findPrev() {
  if (findHits_.empty()) return;
  const int n = static_cast<int>(findHits_.size());
  findCur_ = (findCur_ - 1 + n) % n;
  update();
  if (!findHits_[findCur_].rects.empty())
    emit findScrollTo(findHits_[findCur_].rects.front());
}

void RenderPane::clearFind() {
  findQuery_.clear();
  findHits_.clear();
  findCur_ = -1;
  update();
}

void RenderPane::scrollToAnchor(const QString& id) {
  if (!doc_ || !doc_->root() || id.isEmpty()) return;
  const litehtml::element::ptr el = findById(doc_->root(), id.toStdString());
  if (!el) return;
  const litehtml::position p = el->get_placement();
  emit findScrollTo(QRect(int(p.x), int(p.y), std::max(1, int(p.width)),
                          std::max(1, int(p.height))));
}

void RenderPane::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& m) {
  if (!m.image.empty()) return;
  auto* p = reinterpret_cast<QPainter*>(hdc);
  p->save();
  p->setPen(Qt::NoPen);
  p->setBrush(toQColor(m.color));
  p->drawEllipse(QRectF(static_cast<float>(m.pos.x), static_cast<float>(m.pos.y),
                        static_cast<float>(m.pos.width), static_cast<float>(m.pos.height)));
  p->restore();
}

void RenderPane::load_image(const char* src, const char*, bool) {
  if (src && *src && imgCache_.find(src) == imgCache_.end())
    wantedImg_.insert(src);  // the owner fetches it, then provideImage + rebuild
}

void RenderPane::get_image_size(const char* src, const char*, litehtml::size& sz) {
  auto it = src ? imgCache_.find(src) : imgCache_.end();
  if (it != imgCache_.end() && !it->second.isNull()) {
    sz.width = litehtml::pixel_t(it->second.width());
    sz.height = litehtml::pixel_t(it->second.height());
  } else {
    sz.width = 0;
    sz.height = 0;
  }
}

void RenderPane::draw_image(litehtml::uint_ptr hdc,
                            const litehtml::background_layer& layer,
                            const std::string& url, const std::string&) {
  auto it = imgCache_.find(url);
  if (it == imgCache_.end() || it->second.isNull()) return;
  auto* p = reinterpret_cast<QPainter*>(hdc);
  const auto& b = layer.border_box;
  const QRectF rc(static_cast<float>(b.x), static_cast<float>(b.y),
                  static_cast<float>(b.width), static_cast<float>(b.height));
  p->drawImage(rc, it->second);
}

void RenderPane::draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                 const litehtml::web_color& color) {
  if (color.alpha == 0) return;
  auto* p = reinterpret_cast<QPainter*>(hdc);
  const QColor c = toQColor(color);
  if (layer.is_root) {
    p->fillRect(paintRect_, c);
    return;
  }
  const auto& b = layer.border_box;
  const QRectF rc(static_cast<float>(b.x), static_cast<float>(b.y),
                  static_cast<float>(b.width), static_cast<float>(b.height));
  const float r = static_cast<float>(layer.border_radius.top_left_x);
  p->save();
  p->setPen(Qt::NoPen);
  p->setBrush(c);
  if (r > 0.5f)
    p->drawRoundedRect(rc, r, r);
  else
    p->fillRect(rc, c);
  p->restore();
}

void RenderPane::draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                                      const litehtml::background_layer::linear_gradient&) {}
void RenderPane::draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                                      const litehtml::background_layer::radial_gradient&) {}
void RenderPane::draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                                     const litehtml::background_layer::conic_gradient&) {}

void RenderPane::draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& b,
                              const litehtml::position& pos, bool) {
  auto* p = reinterpret_cast<QPainter*>(hdc);
  p->save();
  p->setPen(Qt::NoPen);
  auto side = [&](float x, float y, float w, float h, const litehtml::border& bd) {
    if (static_cast<float>(bd.width) <= 0.0f || bd.style == litehtml::border_style_none ||
        bd.style == litehtml::border_style_hidden || bd.color.alpha == 0)
      return;
    p->fillRect(QRectF(x, y, w, h), toQColor(bd.color));
  };
  const float x = static_cast<float>(pos.x);
  const float y = static_cast<float>(pos.y);
  const float w = static_cast<float>(pos.width);
  const float h = static_cast<float>(pos.height);
  side(x, y, w, static_cast<float>(b.top.width), b.top);
  side(x, y + h - static_cast<float>(b.bottom.width), w, static_cast<float>(b.bottom.width),
       b.bottom);
  side(x, y, static_cast<float>(b.left.width), h, b.left);
  side(x + w - static_cast<float>(b.right.width), y, static_cast<float>(b.right.width), h,
       b.right);
  p->restore();
}

void RenderPane::set_caption(const char*) {}
void RenderPane::set_base_url(const char*) {}
void RenderPane::link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) {}

void RenderPane::on_anchor_click(const char* url, const litehtml::element::ptr&) {
  if (url && !suppressLinkActivation_) emit linkClicked(QString::fromUtf8(url));
}

void RenderPane::on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) {}
void RenderPane::set_cursor(const char* cursor) {
  const bool link = cursor && std::string(cursor) == "pointer";
  setCursor(link ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void RenderPane::transform_text(std::string& text, litehtml::text_transform tt) {
  QString s = QString::fromUtf8(text.c_str());
  switch (tt) {
    case litehtml::text_transform_uppercase:
      s = s.toUpper();
      break;
    case litehtml::text_transform_lowercase:
      s = s.toLower();
      break;
    case litehtml::text_transform_capitalize: {
      QString out;
      bool start = true;
      for (const QChar ch : s) {
        if (ch.isSpace()) {
          start = true;
          out += ch;
        } else {
          out += start ? ch.toUpper() : ch;
          start = false;
        }
      }
      s = out;
      break;
    }
    default:
      return;
  }
  text = s.toUtf8().constData();
}

void RenderPane::import_css(std::string& text, const std::string& url,
                            std::string&) {
  auto it = cssCache_.find(url);
  if (it != cssCache_.end())
    text = it->second;       // fetched already -> apply it on this re-parse
  else
    wantedCss_.insert(url);  // record; the owner fetches, then we rebuild()
}
void RenderPane::set_clip(const litehtml::position&, const litehtml::border_radiuses&) {}
void RenderPane::del_clip() {}

void RenderPane::get_viewport(litehtml::position& v) const {
  v.x = 0;
  v.y = 0;
  v.width = litehtml::pixel_t(viewport_.width());
  v.height = litehtml::pixel_t(viewport_.height());
}

litehtml::element::ptr RenderPane::create_element(
    const char* tag, const litehtml::string_map& attrs,
    const std::shared_ptr<litehtml::document>& doc) {
  if (tag && (std::strcmp(tag, "object") == 0 || std::strcmp(tag, "embed") == 0)) {
    const auto it = attrs.find("type");
    if (it != attrs.end() &&
        cwb::WidgetRegistry::instance().has(QString::fromStdString(it->second)))
      return std::make_shared<EmbedElement>(doc);
  }
  return {};  // default litehtml element (unknown type keeps its fallback)
}

void RenderPane::get_media_features(litehtml::media_features& m) const {
  m.type = litehtml::media_type_screen;
  m.width = litehtml::pixel_t(viewport_.width());
  m.height = litehtml::pixel_t(viewport_.height());
  m.device_width = m.width;
  m.device_height = m.height;
  m.color = 8;
  m.monochrome = 0;
  m.color_index = 256;
  m.resolution = litehtml::pixel_t(96);
}

void RenderPane::get_language(std::string& language, std::string& culture) const {
  language = "en";
  culture = "";
}

QString cwbSampleDashboardHtml() {
  return QStringLiteral(R"HTML(<!doctype html><html><head><meta charset=utf-8><style>
body{margin:0;background:#0d0f14;color:#e6e9ef;font-family:sans-serif;font-size:14px}
#wrap{margin:0 auto;padding:22px;max-width:860px}
h1{font-size:20px;font-weight:bold;margin:2px 0}
.sub{color:#8b93a3;font-size:12px;margin:6px 0}
.cards{margin:16px 0}
.card{display:inline-block;background:#161a22;border:1px solid #232833;border-radius:10px;padding:12px 14px;margin:0 8px 8px 0;width:150px;vertical-align:top}
.card .v{font-size:22px;font-weight:bold;color:#e6e9ef}
.card .l{color:#8b93a3;font-size:11px;text-transform:uppercase}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:14px}
th{text-align:left;color:#8b93a3;font-weight:bold;padding:7px 8px;border-bottom:1px solid #313847}
td{padding:7px 8px;border-bottom:1px solid #1c2029}
code{background:#20242e;padding:2px 6px;border-radius:4px;color:#c9cedb}
.dot{color:#39d98a;font-size:11px}
.off{color:#6b7280;font-size:11px}
.g{color:#8b93a3}
</style></head><body><div id=wrap>
<h1>CES server monitor</h1>
<div class=sub>server <code>3af9c1e0b7d24f58</code> &middot; version 0.9.1 &middot; served over /ces/lua/1</div>
<div class=cards>
<div class=card><div class=v>128,540</div><div class=l>circulating</div></div>
<div class=card><div class=v>4,213</div><div class=l>accounts</div></div>
<div class=card><div class=v>1,987</div><div class=l>assets</div></div>
<div class=card><div class=v>642</div><div class=l>aliases</div></div>
<div class=card><div class=v>58,301</div><div class=l>transactions</div></div>
<div class=card><div class=v>12</div><div class=l>peers</div></div>
</div>
<table>
<tr><th>peer</th><th>address</th><th>dir</th><th>rpc</th><th>reserve</th></tr>
<tr><td><span class=dot>&#9679;</span> <code>a17f2b9e02c4</code></td><td>node1.example.net:41100</td><td>both</td><td>53831</td><td class=g>4,120</td></tr>
<tr><td><span class=dot>&#9679;</span> <code>c93b6d11aa78</code></td><td>node2.example.net:41100</td><td>out</td><td>53831</td><td class=g>2,050</td></tr>
<tr><td><span class=off>&#9679;</span> <code>0f4277cd51e9</code></td><td>node3.example.net:41100</td><td>in</td><td>53831</td><td class=g>980</td></tr>
</table>
</div></body></html>)HTML");
}
