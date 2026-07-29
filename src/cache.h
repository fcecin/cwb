#pragma once
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <optional>

namespace cwb {

// A 1996-style browser disk cache: the filesystem is the whole system. Each
// entry is one file named sha256(url) in `dir`; the file's mtime is the LRU
// stamp (touched on every hit); eviction is a directory scan on store that
// deletes oldest-first until the total fits `maxBytes`. No RAM index, no
// manifest. Atomic store via temp+rename, so multiple cwb processes can share
// the dir: a racing eviction is at worst a miss. Only successful fetches are
// stored; an entry larger than maxBytes/8 is not cached (it would evict
// everything else for one page).
class DiskCache {
 public:
  static constexpr qint64 kDefaultMaxBytes = 128 * 1024 * 1024;  // 128 MB

  // The standard per-user cache dir (~/.cache/ces/cwb/pages on Linux).
  static QString defaultDir();

  explicit DiskCache(const QString& dir = defaultDir(),
                     qint64 maxBytes = kDefaultMaxBytes);

  // The cached bytes for `url`, refreshing its LRU stamp; nullopt = miss.
  std::optional<QByteArray> get(const QString& url);

  // Store `url` -> bytes, then evict oldest entries until the total fits.
  void put(const QString& url, const QByteArray& bytes);

  void remove(const QString& url);
  void clear();
  qint64 totalBytes() const;  // directory scan

 private:
  QString pathFor(const QString& url) const;
  void evictToFit();

  QString dir_;
  qint64 maxBytes_;
  mutable QMutex mu_;  // in-process; cross-process safety is rename+miss
};

}  // namespace cwb
