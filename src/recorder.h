#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QVector>
#include <cstdint>

class QWidget;
class QTimer;
class QTcpServer;

// Self-recording frame buffer + localhost query API. Grabs the target widget
// every intervalMs into a RAM-only ring (byte-capped; oldest frames evicted),
// hashes each frame's pixels, and serves a tiny HTTP API on 127.0.0.1 so an
// external driver can DETECT flicker (async transitions) programmatically and
// pull individual frames -- all without touching disk. Opt-in; nothing runs
// until start(). One purpose: catch the flickering that piles up when async
// paths clobber each other, using RAM + computer vision instead of human eyes.
//
// Endpoints (all localhost):
//   GET /stats            server + buffer summary (json)
//   GET /flicker[?n=K]    transition/distinct-state report over the last K
//                         frames (default all): the flicker detector (json)
//   GET /frames[?n=K]     per-frame metadata list {seq,t_ms,hash,bytes} (json)
//   GET /frame/<seq>      one frame as PNG (image/png)
//   GET /latest.png       the most recent frame as PNG
class FrameRecorder : public QObject {
  Q_OBJECT
 public:
  FrameRecorder(QWidget* target, int intervalMs, qint64 capBytes, quint16 port,
                QObject* parent = nullptr);
  ~FrameRecorder() override;

  // Begin grabbing and open the query port. Returns false (and sets *err) if the
  // port cannot be bound; grabbing still runs so the buffer is queryable in-proc.
  bool start(QString* err = nullptr);

 private:
  struct Frame {
    quint64 seq;
    qint64 tMs;
    quint64 hash;       // coarse "which state" id (quantized thumbnail)
    QByteArray thumb;   // 32x24 grayscale, for mean-abs-difference comparison
    QByteArray png;     // full frame, for inspection
  };

  void tick();               // grab -> hash -> push -> evict over cap
  void onConnection();       // accept + serve one HTTP request per socket
  QByteArray route(const QString& path, QByteArray& contentType) const;
  QByteArray statsJson() const;
  QByteArray flickerJson(int lastN) const;
  QByteArray framesJson(int lastN) const;

  QWidget* target_ = nullptr;
  int intervalMs_ = 200;
  qint64 capBytes_ = 512ll * 1024 * 1024;
  quint16 port_ = 0;
  QTimer* timer_ = nullptr;
  QTcpServer* server_ = nullptr;
  QElapsedTimer clock_;
  QVector<Frame> ring_;  // oldest at front
  qint64 curBytes_ = 0;
  quint64 seq_ = 0;      // monotonic frame id
  quint64 transitions_ = 0;  // lifetime count of consecutive-hash changes
};
