#include "cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace cwb {

QString DiskCache::defaultDir() {
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
         QStringLiteral("/pages");
}

DiskCache::DiskCache(const QString& dir, qint64 maxBytes)
    : dir_(dir), maxBytes_(maxBytes > 0 ? maxBytes : kDefaultMaxBytes) {
  QDir().mkpath(dir_);
}

QString DiskCache::pathFor(const QString& url) const {
  const QByteArray h =
      QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256);
  return dir_ + "/" + QString::fromLatin1(h.toHex());
}

std::optional<QByteArray> DiskCache::get(const QString& url) {
  QMutexLocker lk(&mu_);
  QFile f(pathFor(url));
  if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
  QByteArray bytes = f.readAll();
  f.close();
  if (bytes.isEmpty()) return std::nullopt;  // stray 0-byte artifact = miss
  // Refresh the LRU stamp; failure only ages the entry early.
  QFile touch(pathFor(url));
  if (touch.open(QIODevice::ReadWrite))
    touch.setFileTime(QDateTime::currentDateTimeUtc(),
                      QFileDevice::FileModificationTime);
  return bytes;
}

void DiskCache::put(const QString& url, const QByteArray& bytes) {
  if (bytes.size() > maxBytes_ / 8) return;  // one page must not own the cache
  QMutexLocker lk(&mu_);
  QDir().mkpath(dir_);
  QSaveFile f(pathFor(url));
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(bytes);
  f.commit();  // atomic temp+rename; sharers see whole entries or nothing
  evictToFit();
}

void DiskCache::remove(const QString& url) {
  QMutexLocker lk(&mu_);
  QFile::remove(pathFor(url));
}

void DiskCache::clear() {
  QMutexLocker lk(&mu_);
  const QFileInfoList all = QDir(dir_).entryInfoList(QDir::Files);
  for (const QFileInfo& fi : all) QFile::remove(fi.absoluteFilePath());
}

qint64 DiskCache::totalBytes() const {
  QMutexLocker lk(&mu_);
  qint64 total = 0;
  const QFileInfoList all = QDir(dir_).entryInfoList(QDir::Files);
  for (const QFileInfo& fi : all) total += fi.size();
  return total;
}

void DiskCache::evictToFit() {
  QFileInfoList all = QDir(dir_).entryInfoList(QDir::Files);
  qint64 total = 0;
  for (const QFileInfo& fi : all) total += fi.size();
  if (total <= maxBytes_) return;
  // Oldest mtime first (the LRU end); delete from the front until we fit.
  std::sort(all.begin(), all.end(), [](const QFileInfo& a, const QFileInfo& b) {
    return a.lastModified() < b.lastModified();
  });
  for (const QFileInfo& fi : all) {
    if (total <= maxBytes_) break;
    total -= fi.size();
    QFile::remove(fi.absoluteFilePath());
  }
}

}  // namespace cwb
