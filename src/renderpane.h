#pragma once
#include <QWidget>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <litehtml.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cwb {
class WidgetContext;
}

// A rich sample page (tables, cards, colors) used only by the `--shot` renderer
// harness. Never shown in the browser.
QString cwbSampleDashboardHtml();

// RenderPane: a litehtml document_container backed by QPainter. Renders HTML/CSS
// (no JavaScript) onto its own surface. It never fetches: the load-resource
// hooks record wanted CSS/image URLs, the owner fetches them over CesPlex and
// feeds them back (provideCss/provideImage), and each delivery relayouts.
class RenderPane : public QWidget, public litehtml::document_container {
  Q_OBJECT
public:
  explicit RenderPane(QWidget* parent = nullptr);
  ~RenderPane() override;

  void setHtml(const QString& html);
  QImage renderToImage(int width);

  // True if any embedded widget reports a dirty state (the "cwbUnsaved" dynamic
  // property), e.g. the writer holding a half-typed story. The window uses this
  // to confirm before navigating away, so text is never silently discarded.
  bool hasUnsavedContent() const;

  // The capability context handed to embedded native widgets (identity, server,
  // navigation). Set by the owning window; widgets use it to act as CES clients.
  void setWidgetContext(cwb::WidgetContext* ctx) { widgetCtx_ = ctx; }

  // Reflow zoom: scale fonts by `z` (clamped 0.5..3.0) and re-lay-out; never
  // a pixel scale.
  void setZoom(double z);
  double zoom() const { return zoom_; }

  // Progressive sub-resources: the render collects wanted CSS/image URLs
  // (verbatim); the owner fetches, feeds back, each delivery relayouts.
  QStringList wantedCss() const;
  QStringList wantedImages() const;
  void provideCss(const QString& url, const QString& text);
  void provideImage(const QString& url, const QImage& img);
  // Mark these raw hrefs (exactly as written on the page) as visited, so they
  // render in the classic visited-purple.
  void setVisitedHrefs(const QStringList& hrefs);

  // Find-in-page. findText highlights every case-insensitive match of `query`
  // (whole-word granularity), selects the first, and asks the owner to scroll to
  // it; findNext/Prev move the selection (wrapping); clearFind removes it. The
  // current index is 1-based for display (0 = none).
  int findText(const QString& query);  // returns match count
  void findNext();
  void findPrev();
  void clearFind();
  int findMatchCount() const { return static_cast<int>(findHits_.size()); }
  int findCurrentIndex() const { return findHits_.empty() ? 0 : findCur_ + 1; }

  // Scroll a same-page "#id" fragment link into view (finds the element whose id
  // attribute matches). Reuses the findScrollTo channel to the owner.
  void scrollToAnchor(const QString& id);

  // Browser-style read-only document selection. There is deliberately no
  // editable caret: these operate on the laid-out HTML text runs.
  QString selectedText() const;
  bool hasSelection() const { return selAnchor_ != selFocus_; }
  void copySelection() const;
  void selectAllText();
  void clearSelection();

signals:
  void linkClicked(const QString& url);
  void hoverLink(const QString& href);  // anchor href under the cursor, or empty
  void findScrollTo(const QRect& docRect);  // make this doc-space rect visible
  // Right-click: `href` is the raw anchor under the cursor (empty = page area).
  void contextMenuRequested(const QString& href, const QPoint& globalPos);

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  QSize sizeHint() const override;

public:
  litehtml::uint_ptr create_font(const litehtml::font_description& descr,
                                 const litehtml::document* doc,
                                 litehtml::font_metrics* fm) override;
  void delete_font(litehtml::uint_ptr hFont) override;
  litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
  void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
                 litehtml::web_color color, const litehtml::position& pos) override;
  litehtml::pixel_t pt_to_px(float pt) const override;
  litehtml::pixel_t get_default_font_size() const override;
  const char* get_default_font_name() const override;
  void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
  void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
  void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
  void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                  const std::string& url, const std::string& base_url) override;
  void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                       const litehtml::web_color& color) override;
  void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                            const litehtml::background_layer::linear_gradient& gradient) override;
  void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                            const litehtml::background_layer::radial_gradient& gradient) override;
  void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                           const litehtml::background_layer::conic_gradient& gradient) override;
  void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders,
                    const litehtml::position& draw_pos, bool root) override;
  void set_caption(const char* caption) override;
  void set_base_url(const char* base_url) override;
  void link(const std::shared_ptr<litehtml::document>& doc,
            const litehtml::element::ptr& el) override;
  void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
  void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;
  void set_cursor(const char* cursor) override;
  void transform_text(std::string& text, litehtml::text_transform tt) override;
  void import_css(std::string& text, const std::string& url, std::string& baseurl) override;
  void set_clip(const litehtml::position& pos,
                const litehtml::border_radiuses& bdr_radius) override;
  void del_clip() override;
  void get_viewport(litehtml::position& viewport) const override;
  litehtml::element::ptr create_element(const char* tag_name,
                                        const litehtml::string_map& attributes,
                                        const std::shared_ptr<litehtml::document>& doc) override;
  void get_media_features(litehtml::media_features& media) const override;
  void get_language(std::string& language, std::string& culture) const override;

private:
  void renderDoc(int width);
  void rebuild();  // re-parse + re-render html_ with the current caches
  // Native embedded widgets: after layout, overlay a native QWidget for every
  // <object>/<embed> whose type is a registered built-in, positioned over its
  // box (the widget is a child of this pane, so it scrolls with the content).
  void placeWidgets();
  void clearWidgets();
  int textPositionAt(const QPoint& point) const;
  int documentTextLength() const;
  std::vector<QRectF> selectionRects() const;

  litehtml::document::ptr doc_;
  QString html_;                                 // current page, for re-parse
  std::map<std::string, std::string> cssCache_;  // url -> css text
  std::map<std::string, QImage> imgCache_;       // url -> decoded image
  std::set<std::string> wantedCss_;              // referenced, not yet cached
  std::set<std::string> wantedImg_;
  std::set<std::string> visitedHrefs_;           // raw hrefs to color as visited
  std::vector<QWidget*> widgets_;                // embedded native widget overlays
  QString widgetsHtml_;                          // the html widgets_ belong to:
                                                 // a relayout of the SAME html
                                                 // reuses them (keeps typed
                                                 // text + focus), never rebuilds
  cwb::WidgetContext* widgetCtx_ = nullptr;      // capability context for widgets
  // Find-in-page state. Each hit is the set of text-run boxes a match spans.
  void rebuildFindHits();       // recompute hit boxes from findQuery_ + the doc
  struct FindHit { std::vector<QRect> rects; };
  std::vector<FindHit> findHits_;
  int findCur_ = -1;            // index into findHits_ of the selected match
  QString findQuery_;
  double zoom_ = 1.0;            // reflow zoom factor applied to every font
  int docWidth_ = 0;
  int docHeight_ = 0;
  QRect paintRect_;
  QSize viewport_{1000, 700};
  QString lastHoverHref_;  // debounce hoverLink emissions
  int selAnchor_ = 0;       // UTF-16 offsets in flattened visible document text
  int selFocus_ = 0;
  bool selecting_ = false;
  bool selectionDragged_ = false;
  bool suppressLinkActivation_ = false;
  QPoint selectionPressPos_;
};
