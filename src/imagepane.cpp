#include "imagepane.h"

#include <QPainter>
#include <QRect>
#include <QSvgRenderer>

ImagePane::ImagePane(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
}

ImagePane::~ImagePane() = default;

bool ImagePane::load(const QByteArray& bytes, bool isSvg) {
  img_ = QImage();
  svg_.reset();
  natural_ = QSize();
  if (isSvg) {
    auto r = std::make_unique<QSvgRenderer>(bytes);
    if (!r->isValid()) return false;
    natural_ = r->defaultSize();
    if (natural_.isEmpty()) natural_ = QSize(300, 150);  // sizeless svg
    svg_ = std::move(r);
    return true;
  }
  QImage img;
  if (!img.loadFromData(bytes)) return false;  // no decoder / corrupt
  img_ = std::move(img);
  natural_ = img_.size();
  updateGeometry();
  return true;
}

void ImagePane::resizeEvent(QResizeEvent*) { update(); }

void ImagePane::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor(0xf0, 0xf2, 0xf5));  // neutral mat, matches chrome
  if (natural_.isEmpty()) return;

  // Fit inside the viewport, preserving aspect, but never upscale past natural.
  QSize shown = natural_;
  if (shown.width() > width() || shown.height() > height())
    shown = shown.scaled(size(), Qt::KeepAspectRatio);
  const QRect target(QPoint((width() - shown.width()) / 2,
                            (height() - shown.height()) / 2),
                     shown);

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (svg_)
    svg_->render(&p, QRectF(target));  // vector: crisp at any size
  else if (!img_.isNull())
    p.drawImage(target, img_);
}
