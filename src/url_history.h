#pragma once
#include <QString>
#include <QStringList>

// Persistent, most-recent-first list of visited addresses, backing the address
// bar's autocomplete and dropdown. Stored as a plain text file (one URL per
// line), deduped case-insensitively, capped. Widgets-free (lives in cwb_core) so
// it is unit-tested; the app hands it a path under AppDataLocation.
class UrlHistory {
 public:
  explicit UrlHistory(QString filePath);  // loads existing entries if present
  // Record a visit: move/insert `url` at the front (case-insensitive dedup),
  // trim to the cap, and persist. Empty/blank urls are ignored.
  void add(const QString& url);
  void remove(const QString& url);            // drop an entry (case-insensitive)
  bool contains(const QString& url) const;    // case-insensitive membership
  const QStringList& entries() const { return entries_; }  // most-recent-first
  void clear();  // wipe the list and delete the file
  static int cap() { return 500; }

 private:
  void load();
  void save() const;
  QString path_;
  QStringList entries_;
};
