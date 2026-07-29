#include "worktree.h"

#include <QDate>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace cwb {

namespace {

const char* kSrcSuffix = ".src.txt";
const char* kMetaSuffix = ".work.toml";

QString metaPath(const QString& local) { return local + kMetaSuffix; }

QString draftPath(const QString& key) {
  if (key.isEmpty()) return {};
  const QByteArray name =
      QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
  const QString dir = workRoot() + QStringLiteral("/drafts");
  QDir().mkpath(dir);
  return dir + QLatin1Char('/') + QString::fromLatin1(name) +
         QStringLiteral(".json");
}

// Minimal line-based sidecar: title = "...", date = "...", remotes = "a,b".
// Same spirit as the store's .sidecar.toml; hand-rolled to stay dep-free.
struct Meta {
  QString title, date;
  QStringList remotes;
  bool retired = false;
};

Meta readMeta(const QString& local) {
  Meta m;
  QFile f(metaPath(local));
  if (!f.open(QIODevice::ReadOnly)) return m;
  for (const QByteArray& raw : f.readAll().split('\n')) {
    const QString line = QString::fromUtf8(raw).trimmed();
    const auto val = [&line]() {
      QString v = line.section('=', 1).trimmed();
      if (v.startsWith('"') && v.endsWith('"') && v.size() >= 2)
        v = v.mid(1, v.size() - 2);
      return v;
    };
    if (line.startsWith(QLatin1String("title")))
      m.title = val();
    else if (line.startsWith(QLatin1String("date")))
      m.date = val();
    else if (line.startsWith(QLatin1String("remotes"))) {
      for (const QString& r : val().split(',', Qt::SkipEmptyParts))
        m.remotes << r.trimmed();
    } else if (line.startsWith(QLatin1String("retired")))
      m.retired = (val() == QLatin1String("true"));
  }
  return m;
}

bool writeAtomic(const QString& path, const QByteArray& bytes) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  if (f.write(bytes) != bytes.size()) return false;
  return f.commit();
}

bool writeMeta(const QString& local, const Meta& m) {
  QString t = m.title;
  t.replace('"', QLatin1String("'"));
  QString out =
      QStringLiteral("title = \"%1\"\ndate = \"%2\"\nremotes = \"%3\"\n")
          .arg(t, m.date, m.remotes.join(','));
  if (m.retired) out += QStringLiteral("retired = \"true\"\n");
  return writeAtomic(metaPath(local), out.toUtf8());
}

}  // namespace

QString workRoot() {
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      QStringLiteral("/work");
  QDir().mkpath(root);
  return root;
}

QString workLocalPath(const QString& zonePath) {
  if (!zonePath.startsWith('/') || zonePath.contains(QLatin1String("..")))
    return {};
  return workRoot() + zonePath;
}

bool workSave(const QString& zonePath, const QByteArray& artifact,
              const QByteArray& source, const QString& title) {
  const QString local = workLocalPath(zonePath);
  if (local.isEmpty() || artifact.isEmpty()) return false;
  QDir().mkpath(local.left(local.lastIndexOf('/')));
  if (!writeAtomic(local, artifact)) return false;
  if (!source.isEmpty() && !writeAtomic(local + kSrcSuffix, source)) return false;
  Meta m = readMeta(local);  // keep the remotes across re-publishes
  m.title = title;
  m.date = QDate::currentDate().toString(Qt::ISODate);
  m.retired = false;  // publishing is the un-retire gesture
  return writeMeta(local, m);
}

void workNoteRemote(const QString& zonePath, const QString& hostPort) {
  const QString local = workLocalPath(zonePath);
  if (local.isEmpty() || hostPort.isEmpty()) return;
  Meta m = readMeta(local);
  if (!m.remotes.contains(hostPort)) {
    m.remotes << hostPort;
    writeMeta(local, m);
  }
}

QStringList workRemotes(const QString& zonePath) {
  const QString local = workLocalPath(zonePath);
  return local.isEmpty() ? QStringList{} : readMeta(local).remotes;
}

void workSetRetired(const QString& zonePath, bool retired) {
  const QString local = workLocalPath(zonePath);
  if (local.isEmpty() || !QFile::exists(local)) return;
  Meta m = readMeta(local);
  if (m.retired == retired) return;
  m.retired = retired;
  writeMeta(local, m);
}

bool workRetired(const QString& zonePath) {
  const QString local = workLocalPath(zonePath);
  return !local.isEmpty() && readMeta(local).retired;
}

bool workDelete(const QString& zonePath) {
  const QString local = workLocalPath(zonePath);
  if (local.isEmpty()) return false;
  bool gone = true;
  for (const QString& p :
       {local, local + kSrcSuffix, metaPath(local)})
    if (QFile::exists(p) && !QFile::remove(p)) gone = false;
  return gone;
}

QByteArray workArtifact(const QString& zonePath) {
  const QString local = workLocalPath(zonePath);
  if (local.isEmpty()) return {};
  QFile f(local);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

QStringList workList(const QString& zonePrefix) {
  QStringList out;
  if (!zonePrefix.startsWith('/')) return out;
  const QString root = workRoot();
  const QString dir = root + zonePrefix;
  QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString p = it.next();
    if (p.endsWith(QLatin1String(kSrcSuffix)) ||
        p.endsWith(QLatin1String(kMetaSuffix)))
      continue;
    out << p.mid(root.size());  // back to a zone path
  }
  out.sort();
  return out;
}

QString workFindByTitle(const QString& zonePrefix, const QString& title) {
  if (title.isEmpty()) return {};
  for (const QString& zp : workList(zonePrefix))
    if (readMeta(workLocalPath(zp)).title == title) return zp;
  return {};
}

QString workTitle(const QString& zonePath) {
  const QString local = workLocalPath(zonePath);
  return local.isEmpty() ? QString{} : readMeta(local).title;
}

bool draftSave(const QString& key, const QString& title, const QString& body) {
  const QString path = draftPath(key);
  if (path.isEmpty()) return false;
  QJsonObject o;
  o.insert(QStringLiteral("title"), title);
  o.insert(QStringLiteral("body"), body);
  return writeAtomic(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool draftLoad(const QString& key, QString& title, QString& body) {
  QFile f(draftPath(key));
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) return false;
  title = doc.object().value(QStringLiteral("title")).toString();
  body = doc.object().value(QStringLiteral("body")).toString();
  return true;
}

bool draftRemove(const QString& key) {
  const QString path = draftPath(key);
  return !path.isEmpty() && (!QFile::exists(path) || QFile::remove(path));
}

}  // namespace cwb
