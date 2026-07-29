#include "url_history.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <utility>

UrlHistory::UrlHistory(QString filePath) : path_(std::move(filePath)) { load(); }

void UrlHistory::load() {
  entries_.clear();
  QFile f(path_);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
  QTextStream in(&f);
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (!line.isEmpty() && entries_.size() < cap()) entries_ << line;
  }
}

void UrlHistory::add(const QString& url) {
  const QString u = url.trimmed();
  if (u.isEmpty()) return;
  for (int i = entries_.size() - 1; i >= 0; --i)  // case-insensitive dedup
    if (entries_[i].compare(u, Qt::CaseInsensitive) == 0) entries_.removeAt(i);
  entries_.prepend(u);
  while (entries_.size() > cap()) entries_.removeLast();
  save();
}

void UrlHistory::remove(const QString& url) {
  const QString u = url.trimmed();
  if (u.isEmpty()) return;
  bool changed = false;
  for (int i = entries_.size() - 1; i >= 0; --i)
    if (entries_[i].compare(u, Qt::CaseInsensitive) == 0) {
      entries_.removeAt(i);
      changed = true;
    }
  if (changed) save();
}

bool UrlHistory::contains(const QString& url) const {
  const QString u = url.trimmed();
  for (const QString& e : entries_)
    if (e.compare(u, Qt::CaseInsensitive) == 0) return true;
  return false;
}

void UrlHistory::clear() {
  entries_.clear();
  QFile::remove(path_);
}

void UrlHistory::save() const {
  QSaveFile f(path_);  // atomic: writes a temp then renames on commit
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
  QTextStream out(&f);
  for (const QString& e : entries_) out << e << '\n';
  out.flush();
  f.commit();
}
