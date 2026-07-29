#include "recorder.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QPixmap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Thumbnail dimensions for the flicker signature. Small enough that a blinking
// caret / 1px change is negligible, big enough that a page swap is obvious.
constexpr int kThumbW = 32, kThumbH = 24;
// Mean-absolute-difference (0..255) above which two frames count as a real
// visual change. A caret blink is < 1; a page swap is tens.
constexpr int kMadChange = 4;

// A frame's flicker signature: a 32x24 grayscale thumbnail (768 bytes). Frames
// are compared by mean-absolute-difference, not equality, so trivial churn is
// ignored and only meaningful redraws register.
QByteArray makeThumb(const QImage& img) {
  const QImage t =
      img.scaled(kThumbW, kThumbH, Qt::IgnoreAspectRatio,
                 Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_Grayscale8);
  QByteArray b(kThumbW * kThumbH, 0);
  for (int y = 0; y < kThumbH; ++y)
    std::memcpy(b.data() + y * kThumbW, t.constScanLine(y), kThumbW);
  return b;
}

// FNV-1a of a heavily quantized thumbnail (3-bit gray): a coarse "which state"
// id for readouts. Quantization keeps sub-threshold noise from minting states.
quint64 hashThumb(const QByteArray& thumb) {
  quint64 h = 1469598103934665603ull;
  for (unsigned char c : thumb) {
    h ^= static_cast<quint64>(c >> 5);
    h *= 1099511628211ull;
  }
  return h;
}

// Mean absolute difference of two equal-size thumbnails, 0..255.
int madThumb(const QByteArray& a, const QByteArray& b) {
  if (a.size() != b.size() || a.isEmpty()) return 255;
  quint64 sum = 0;
  const auto* pa = reinterpret_cast<const unsigned char*>(a.constData());
  const auto* pb = reinterpret_cast<const unsigned char*>(b.constData());
  for (int i = 0; i < a.size(); ++i)
    sum += static_cast<quint64>(std::abs(int(pa[i]) - int(pb[i])));
  return static_cast<int>(sum / a.size());
}

QByteArray httpResponse(const QByteArray& body, const char* ctype,
                        int status = 200) {
  QByteArray r;
  r += "HTTP/1.0 " + QByteArray::number(status) +
       (status == 200 ? " OK\r\n" : " ERR\r\n");
  r += "Content-Type: " + QByteArray(ctype) + "\r\n";
  r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  r += "Access-Control-Allow-Origin: *\r\n";
  r += "Connection: close\r\n\r\n";
  r += body;
  return r;
}

int queryN(const QString& path, int fallback) {
  const int q = path.indexOf(QLatin1String("n="));
  if (q < 0) return fallback;
  bool ok = false;
  const int v = path.mid(q + 2).split('&').first().toInt(&ok);
  return (ok && v > 0) ? v : fallback;
}

}  // namespace

FrameRecorder::FrameRecorder(QWidget* target, int intervalMs, qint64 capBytes,
                             quint16 port, QObject* parent)
    : QObject(parent),
      target_(target),
      intervalMs_(intervalMs > 0 ? intervalMs : 200),
      capBytes_(capBytes > 0 ? capBytes : 512ll * 1024 * 1024),
      port_(port) {}

FrameRecorder::~FrameRecorder() = default;

bool FrameRecorder::start(QString* err) {
  clock_.start();
  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &FrameRecorder::tick);
  timer_->start(intervalMs_);

  server_ = new QTcpServer(this);
  connect(server_, &QTcpServer::newConnection, this,
          &FrameRecorder::onConnection);
  if (!server_->listen(QHostAddress(QStringLiteral("127.0.0.1")), port_)) {
    if (err) *err = server_->errorString();
    std::fprintf(stderr, "recorder: query port %u FAILED (%s); buffer still "
                         "recording in-process\n",
                 port_, server_->errorString().toUtf8().constData());
    return false;
  }
  std::fprintf(stderr,
               "recorder: %d ms/frame, %lld MB cap, query http://127.0.0.1:%u/"
               " (/stats /flicker /frames /frame/<seq> /latest.png)\n",
               intervalMs_, static_cast<long long>(capBytes_ / (1024 * 1024)),
               port_);
  return true;
}

void FrameRecorder::tick() {
  if (!target_) return;
  const QImage img = target_->grab().toImage();
  if (img.isNull()) return;
  const QByteArray thumb = makeThumb(img);
  const quint64 h = hashThumb(thumb);
  QByteArray png;
  {
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
  }
  if (!ring_.isEmpty() && madThumb(ring_.back().thumb, thumb) >= kMadChange)
    ++transitions_;
  ring_.push_back(Frame{seq_++, clock_.elapsed(), h, thumb, png});
  curBytes_ += png.size() + thumb.size();
  while (curBytes_ > capBytes_ && ring_.size() > 1) {
    curBytes_ -= ring_.front().png.size() + ring_.front().thumb.size();
    ring_.pop_front();
  }
}

void FrameRecorder::onConnection() {
  while (QTcpSocket* sock = server_ ? server_->nextPendingConnection()
                                    : nullptr) {
    connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
      const QByteArray req = sock->readAll();
      const int sp1 = req.indexOf(' ');
      const int sp2 = req.indexOf(' ', sp1 + 1);
      QString path = (sp1 >= 0 && sp2 > sp1)
                         ? QString::fromUtf8(req.mid(sp1 + 1, sp2 - sp1 - 1))
                         : QStringLiteral("/");
      QByteArray ctype = "application/json";
      const QByteArray body = route(path, ctype);
      sock->write(httpResponse(body, ctype.constData()));
      sock->flush();
      sock->disconnectFromHost();
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }
}

QByteArray FrameRecorder::route(const QString& path,
                                QByteArray& contentType) const {
  if (path.startsWith(QLatin1String("/flicker")))
    return flickerJson(queryN(path, 0));
  if (path.startsWith(QLatin1String("/frames")))
    return framesJson(queryN(path, 0));
  if (path.startsWith(QLatin1String("/frame/"))) {
    bool ok = false;
    const quint64 want = path.mid(7).split('.').first().toULongLong(&ok);
    if (ok)
      for (const Frame& f : ring_)
        if (f.seq == want) {
          contentType = "image/png";
          return f.png;
        }
    return QByteArrayLiteral("{\"error\":\"not found\"}");
  }
  if (path.startsWith(QLatin1String("/latest.png"))) {
    if (!ring_.isEmpty()) {
      contentType = "image/png";
      return ring_.back().png;
    }
    return QByteArrayLiteral("{\"error\":\"not found\"}");
  }
  return statsJson();
}

QByteArray FrameRecorder::statsJson() const {
  const qint64 span = ring_.isEmpty() ? 0 : ring_.back().tMs - ring_.front().tMs;
  QByteArray b = "{";
  b += "\"interval_ms\":" + QByteArray::number(intervalMs_);
  b += ",\"frames\":" + QByteArray::number(ring_.size());
  b += ",\"span_ms\":" + QByteArray::number(span);
  b += ",\"bytes\":" + QByteArray::number(curBytes_);
  b += ",\"cap_bytes\":" + QByteArray::number(capBytes_);
  b += ",\"total_grabbed\":" + QByteArray::number(seq_);
  b += ",\"lifetime_transitions\":" + QByteArray::number(transitions_);
  if (!ring_.isEmpty())
    b += ",\"latest_seq\":" + QByteArray::number(ring_.back().seq);
  b += "}";
  return b;
}

QByteArray FrameRecorder::flickerJson(int lastN) const {
  const int n = (lastN > 0 && lastN < ring_.size()) ? lastN : ring_.size();
  const int from = ring_.size() - n;
  // Transitions: consecutive frames whose thumbnails differ by >= kMadChange
  // (a real redraw), so a blinking caret never counts. Distinct states: greedy
  // MAD clustering -- a frame joins the first cluster it resembles, else opens
  // a new one. Flicker = the view bounces (>=4 real changes) among few states.
  int transitions = 0, maxMad = 0;
  QVector<QByteArray> clusters;
  for (int i = from; i < ring_.size(); ++i) {
    if (i > from) {
      const int d = madThumb(ring_[i - 1].thumb, ring_[i].thumb);
      if (d > maxMad) maxMad = d;
      if (d >= kMadChange) ++transitions;
    }
    bool matched = false;
    for (const QByteArray& c : clusters)
      if (madThumb(c, ring_[i].thumb) < kMadChange) { matched = true; break; }
    if (!matched) clusters.push_back(ring_[i].thumb);
  }
  const qint64 span = n > 0 ? ring_.back().tMs - ring_[from].tMs : 0;
  const bool flicker = transitions >= 4 && clusters.size() >= 2 &&
                       clusters.size() <= 3;
  QByteArray b = "{";
  b += "\"window_frames\":" + QByteArray::number(n);
  b += ",\"span_ms\":" + QByteArray::number(span);
  b += ",\"transitions\":" + QByteArray::number(transitions);
  b += ",\"distinct_states\":" + QByteArray::number(clusters.size());
  b += ",\"max_mad\":" + QByteArray::number(maxMad);
  b += ",\"flicker\":";
  b += flicker ? "true" : "false";
  b += ",\"tail\":[";
  const int tailFrom = ring_.size() > 12 ? ring_.size() - 12 : 0;
  for (int i = tailFrom; i < ring_.size(); ++i) {
    if (i > tailFrom) b += ',';
    b += "{\"seq\":" + QByteArray::number(ring_[i].seq) +
         ",\"t_ms\":" + QByteArray::number(ring_[i].tMs) +
         ",\"hash\":\"" + QByteArray::number(ring_[i].hash, 16) + "\"}";
  }
  b += "]}";
  return b;
}

QByteArray FrameRecorder::framesJson(int lastN) const {
  const int n = (lastN > 0 && lastN < ring_.size()) ? lastN : ring_.size();
  const int from = ring_.size() - n;
  QByteArray b = "[";
  for (int i = from; i < ring_.size(); ++i) {
    if (i > from) b += ',';
    b += "{\"seq\":" + QByteArray::number(ring_[i].seq) +
         ",\"t_ms\":" + QByteArray::number(ring_[i].tMs) +
         ",\"hash\":\"" + QByteArray::number(ring_[i].hash, 16) +
         "\",\"bytes\":" + QByteArray::number(ring_[i].png.size()) + "}";
  }
  b += "]";
  return b;
}
