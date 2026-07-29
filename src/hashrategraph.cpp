#include "hashrategraph.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

HashrateGraph::HashrateGraph(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(160);
  setAttribute(Qt::WA_StyledBackground, false);
}

void HashrateGraph::addSample(double hps) {
  samples_.append(hps < 0 ? 0 : hps);
  while (samples_.size() > capacity_) samples_.removeFirst();
  update();
}

void HashrateGraph::clear() {
  samples_.clear();
  update();
}

static QString fmtHps(double v) {
  if (v >= 1e6) return QString::number(v / 1e6, 'f', 1) + "M";
  if (v >= 1e3) return QString::number(v / 1e3, 'f', 1) + "k";
  return QString::number(v, 'f', 0);
}

void HashrateGraph::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF full = rect().adjusted(0.5, 0.5, -0.5, -0.5);
  p.fillRect(full, QColor(0x0e, 0x12, 0x17));

  // Left gutter holds the y-axis scale labels; the series plots to its right.
  constexpr double gutter = 56.0;
  const QRectF r = full.adjusted(gutter, 6, -6, -6);

  const bool haveData = samples_.size() >= 2;
  const double peak =
      haveData ? std::max(1.0, *std::max_element(samples_.begin(), samples_.end()))
               : 0.0;

  // Gridlines + right-aligned scale labels (top = peak, bottom = 0). Redrawn
  // every frame so the legend tracks the autoscaling.
  QFont f = p.font();
  f.setPixelSize(10);
  p.setFont(f);
  for (int i = 0; i <= 4; ++i) {
    const double y = r.top() + r.height() * i / 4.0;
    p.setPen(QPen(QColor(0x22, 0x2a, 0x35), 1));
    if (i > 0 && i < 4) p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    if (haveData) {
      p.setPen(QColor(0x7b, 0x87, 0x98));
      const double val = peak * (1.0 - i / 4.0);
      p.drawText(QRectF(0, y - 7, gutter - 8, 14),
                 Qt::AlignRight | Qt::AlignVCenter, fmtHps(val));
    }
  }
  p.setPen(QColor(0x59, 0x63, 0x72));
  p.drawText(QRectF(0, full.top() + 2, gutter - 8, 12),
             Qt::AlignRight | Qt::AlignTop, tr("H/s"));

  if (!haveData) {
    p.setPen(QColor(0x6b, 0x72, 0x80));
    p.drawText(r, Qt::AlignCenter, tr("waiting for hashes ..."));
    return;
  }

  const double n = capacity_ - 1;
  auto pt = [&](int i) {
    // Right-align the newest sample; older samples trail to the left.
    const double x = r.right() - (samples_.size() - 1 - i) * (r.width() / n);
    const double y = r.bottom() - (samples_[i] / peak) * (r.height() - 6);
    return QPointF(x, y);
  };

  QPainterPath line(pt(0));
  for (int i = 1; i < samples_.size(); ++i) line.lineTo(pt(i));

  QPainterPath area = line;
  area.lineTo(QPointF(pt(samples_.size() - 1).x(), r.bottom()));
  area.lineTo(QPointF(pt(0).x(), r.bottom()));
  area.closeSubpath();

  QLinearGradient g(0, r.top(), 0, r.bottom());
  g.setColorAt(0, QColor(line_.red(), line_.green(), line_.blue(), 120));
  g.setColorAt(1, QColor(line_.red(), line_.green(), line_.blue(), 8));
  p.fillPath(area, g);
  p.setPen(QPen(line_, 2));
  p.drawPath(line);

  // Glow dot on the latest sample.
  const QPointF head = pt(samples_.size() - 1);
  p.setBrush(line_);
  p.setPen(Qt::NoPen);
  p.drawEllipse(head, 3.2, 3.2);
}
