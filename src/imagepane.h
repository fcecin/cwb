#pragma once
#include <QImage>
#include <QSize>
#include <QWidget>
#include <memory>

class QSvgRenderer;

// The inline image viewer: a content pane (like RenderPane/TerminalPane) that
// shows a single CES-hosted image. Raster formats decode once via QImage; SVG
// renders through QSvgRenderer so it stays crisp at any display size. The image
// is centered and scaled DOWN to fit the window, but never scaled up past its
// natural size (a small icon shows 1:1, not blown up). No JavaScript, no fetch:
// the owner fetches the bytes over CesPlex and hands them to load().
class ImagePane : public QWidget {
  Q_OBJECT
 public:
  explicit ImagePane(QWidget* parent = nullptr);
  ~ImagePane() override;

  // Decode raw file bytes. isSvg picks the crisp vector path. Returns false if
  // the bytes don't decode (no image plugin, corrupt data) -- the caller then
  // falls back to a save dialog, so an undecodable image never blanks.
  bool load(const QByteArray& bytes, bool isSvg);
  QSize imageSize() const { return natural_; }  // natural pixel size

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  QSize sizeHint() const override { return natural_; }

 private:
  QImage img_;                          // raster path (null if svg)
  std::unique_ptr<QSvgRenderer> svg_;   // vector path (null if raster)
  QSize natural_;                       // natural image size in px
};
