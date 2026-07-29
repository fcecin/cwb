#include "mediapane.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoWidget>

MediaPane::MediaPane(const QByteArray& bytes, bool audioOnly, QWidget* parent)
    : QWidget(parent), data_(bytes) {
  buffer_ = new QBuffer(&data_, this);
  buffer_->open(QIODevice::ReadOnly);
  player_ = new QMediaPlayer(this);
  audio_ = new QAudioOutput(this);
  player_->setAudioOutput(audio_);

  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  if (!audioOnly) {
    video_ = new QVideoWidget(this);
    video_->setStyleSheet(QStringLiteral("background:#000"));
    player_->setVideoOutput(video_);
    col->addWidget(video_, 1);
  } else {
    col->addStretch(1);
  }

  controls_ = new QWidget(this);
  auto* row = new QHBoxLayout(controls_);
  row->setContentsMargins(10, 8, 10, 8);
  playBtn_ = new QToolButton(this);
  playBtn_->setText(QStringLiteral("⏸"));  // pause glyph (we autoplay)
  playBtn_->setToolTip(tr("Play/Pause"));
  connect(playBtn_, &QToolButton::clicked, this, &MediaPane::togglePlay);
  row->addWidget(playBtn_);

  seek_ = new QSlider(Qt::Horizontal, this);
  seek_->setRange(0, 0);
  connect(seek_, &QSlider::sliderPressed, this, [this] { seekHeld_ = true; });
  connect(seek_, &QSlider::sliderReleased, this, [this] {
    player_->setPosition(seek_->value());
    seekHeld_ = false;
  });
  row->addWidget(seek_, 1);

  time_ = new QLabel(QStringLiteral("0:00 / 0:00"), this);
  time_->setStyleSheet(QStringLiteral("font-family:monospace;color:#586069;padding:0 6px"));
  row->addWidget(time_);

  auto* vol = new QSlider(Qt::Horizontal, this);
  vol->setRange(0, 100);
  vol->setValue(100);
  vol->setMaximumWidth(90);
  vol->setToolTip(tr("Volume"));
  connect(vol, &QSlider::valueChanged, this,
          [this](int val) { audio_->setVolume(val / 100.0f); });
  row->addWidget(new QLabel(QStringLiteral("\U0001F509"), this));
  row->addWidget(vol);
  col->addWidget(controls_);
  if (audioOnly) col->addStretch(1);

  connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
    duration_ = d;
    seek_->setRange(0, static_cast<int>(d));
    updateTimeLabel();
  });
  connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
    if (!seekHeld_) seek_->setValue(static_cast<int>(p));
    updateTimeLabel();
  });
  connect(player_, &QMediaPlayer::playbackStateChanged, this,
          [this](QMediaPlayer::PlaybackState s) {
            playBtn_->setText(s == QMediaPlayer::PlayingState
                                  ? QStringLiteral("⏸")
                                  : QStringLiteral("▶"));
          });
  connect(player_, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error e, const QString&) {
            if (e != QMediaPlayer::NoError) showError();
          });

  player_->setSourceDevice(buffer_);
  player_->play();
}

MediaPane::~MediaPane() {
  // Stop and detach from data_/buffer_ before members and children are torn
  // down, so the backend never reads freed bytes.
  if (player_) {
    player_->stop();
    player_->setSourceDevice(nullptr);
  }
}

void MediaPane::showError() {
  if (errorShown_) return;  // errors can fire more than once
  errorShown_ = true;
  if (player_) player_->stop();
  if (video_) video_->hide();
  if (controls_) controls_->hide();
  auto* err = new QWidget(this);
  auto* v = new QVBoxLayout(err);
  v->addStretch(1);
  auto* msg = new QLabel(tr("This media file could not be played."), err);
  msg->setAlignment(Qt::AlignCenter);
  msg->setStyleSheet(QStringLiteral("font-size:15px;color:#586069"));
  v->addWidget(msg);
  auto* h = new QHBoxLayout;
  auto* btn = new QPushButton(tr("Download instead"), err);
  connect(btn, &QPushButton::clicked, this, &MediaPane::downloadRequested);
  h->addStretch(1);
  h->addWidget(btn);
  h->addStretch(1);
  v->addLayout(h);
  v->addStretch(1);
  if (auto* col = qobject_cast<QVBoxLayout*>(layout())) col->addWidget(err, 1);
}

void MediaPane::togglePlay() {
  if (player_->playbackState() == QMediaPlayer::PlayingState)
    player_->pause();
  else
    player_->play();
}

QString MediaPane::fmtTime(qint64 ms) {
  const qint64 s = (ms < 0 ? 0 : ms) / 1000;
  return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

void MediaPane::updateTimeLabel() {
  time_->setText(fmtTime(player_->position()) + QStringLiteral(" / ") +
                 fmtTime(duration_));
}
