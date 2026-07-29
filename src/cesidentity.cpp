#include "cesidentity.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace cwb {

static QString dataPath(const char* leaf) {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/" + leaf;
}

static QString readSmall(const QString& path) {
  QFile f(path);
  if (f.exists() && f.open(QIODevice::ReadOnly))
    return QString::fromUtf8(f.readAll()).trimmed();
  return {};
}

static void writeSmall(const QString& path, const QString& text,
                       bool secret) {
  QFile w(path);
  if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (secret)  // narrow permissions before the bytes land in the file
      w.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    w.write(text.toUtf8());
    w.close();
  }
}

static std::optional<ces::KeyPair> loadKeyFile(const QString& path) {
  const QString hex = readSmall(path);
  if (hex.size() == 64) {
    try {
      return ces::KeyPair(hex.toStdString(), ces::KeyAlgo::ED25519);
    } catch (...) {
    }
  }
  return std::nullopt;
}

ces::KeyPair loadOrCreateIdentity() {
  const QString path = dataPath("identity.key");
  if (auto kp = loadKeyFile(path)) return *kp;
  ces::KeyPair kp;  // generates a fresh ED25519 keypair
  writeSmall(path, QString::fromStdString(kp.getPrivateKeyHexStr()), true);
  return kp;
}

ces::KeyPair authorIdentity() { return loadOrCreateIdentity(); }

QString preferredName() { return readSmall(dataPath("name")); }

void setPreferredName(const QString& name) {
  writeSmall(dataPath("name"), name, false);
}

}  // namespace cwb
