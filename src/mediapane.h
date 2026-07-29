#pragma once
#include <QByteArray>
#include <QWidget>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QBuffer;
class QToolButton;
class QSlider;
class QLabel;

// In-window audio/video player: a content pane (like RenderPane/ImagePane) that
// plays a CES-hosted media file. The bytes arrive over CesPlex and are played
// from memory (QBuffer), so no temp file. Video formats show a QVideoWidget plus
// transport controls; audio formats show just the controls. If the multimedia
// backend rejects the file, it emits failed() so the owner can fall back to a
// plain download -- media never dead-ends.
class MediaPane : public QWidget {
  Q_OBJECT
 public:
  MediaPane(const QByteArray& bytes, bool audioOnly, QWidget* parent = nullptr);
  ~MediaPane() override;

 signals:
  void downloadRequested();  // user chose to save an unplayable file instead

 private:
  static QString fmtTime(qint64 ms);
  void togglePlay();
  void updateTimeLabel();
  void showError();  // backend rejected the media: in-pane message + Download

  QByteArray data_;  // owns the media bytes for the player's lifetime
  QBuffer* buffer_ = nullptr;
  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audio_ = nullptr;
  QVideoWidget* video_ = nullptr;  // null for audio-only
  QWidget* controls_ = nullptr;    // transport row (hidden on error)
  QToolButton* playBtn_ = nullptr;
  QSlider* seek_ = nullptr;
  QLabel* time_ = nullptr;
  bool seekHeld_ = false;
  bool errorShown_ = false;
  qint64 duration_ = 0;
};
