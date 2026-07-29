#pragma once
#include <QColor>
#include <QVector>
#include <QWidget>

// A rolling area chart of recent hashrate, drawn with QPainter (no Qt Charts
// dependency). Autoscales to the tallest sample in the window; the newest
// sample is on the right and the series scrolls left as samples arrive.
class HashrateGraph : public QWidget {
  Q_OBJECT
 public:
  explicit HashrateGraph(QWidget* parent = nullptr);

  void addSample(double hashesPerSec);
  void clear();

 protected:
  void paintEvent(QPaintEvent*) override;
  QSize sizeHint() const override { return {600, 180}; }

 private:
  QVector<double> samples_;
  int capacity_ = 160;  // window length in samples
  QColor line_{0x39, 0xd9, 0x8a};
};
