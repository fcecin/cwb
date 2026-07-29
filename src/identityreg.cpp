#include "identityreg.h"

#include "cesidentity.h"
#include "names.h"
#include "worktree.h"

#include <ces/account.h>
#include <ces/client.h>
#include <ces/l2/file_client.h>
#include <ces/types.h>
#include <ces/util/resolver.h>
#include <ces/util/wallet.h>  // ces::ClientSession

#include <QStringList>

#include <cstring>
#include <string>

namespace cwb {

namespace {

ces::Bytes toBytes(const QString& s) {
  const QByteArray u = s.toUtf8();
  return ces::Bytes(u.begin(), u.end());
}
QString fromBytes(const ces::Bytes& b) {
  return QString::fromUtf8(reinterpret_cast<const char*>(b.data()),
                          static_cast<int>(b.size()));
}

// The name currently registered to `key` on the open session ("" if none).
QString nameForKey(ces::CesClient& cc, const ces::Hash& key) {
  ces::Bytes nm;
  bool found = false;
  if (cc.queryKeyName(key, nm, found) == ces::CES_OK && found)
    return fromBytes(nm);
  return {};
}

// The key currently holding `name` ("" normalized) on the open session; sets
// `taken` and returns the holder prefix hex ("" if free).
bool nameHolder(ces::CesClient& cc, const QString& name, ces::Hash& outKey) {
  bool found = false;
  return cc.queryKeyNameByName(toBytes(name), outKey, found) == ces::CES_OK &&
         found;
}

}  // namespace

bool registerName(const QString& host, uint16_t mainPort, const QString& name,
                  NameStatus& out) {
  out = {};
  const QString nm = canonicalKeyName(name);
  out.server = host + ":" + QString::number(mainPort);
  if (nm.isEmpty()) {
    out.detail = QStringLiteral(
        "invalid name (1..32 bytes, no path characters, no leading . _ -)");
    return true;
  }
  out.name = nm;
  out.handle = nm;  // the canonical name IS the /f/ handle (underscore form)
  try {
    const ces::KeyPair id = loadOrCreateIdentity();
    const ces::Hash me = id.getPublicKeyAsHash();
    const auto ep = ces::Resolver::resolveUdp(
        host.toStdString() + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &id, 3);
    auto& cc = sess.client();

    if (nameForKey(cc, me) == nm) {
      setPreferredName(nm);
      out.ok = true;
      out.detail = QStringLiteral("already registered as %1").arg(nm);
      return true;
    }
    ces::Hash holder{};
    if (nameHolder(cc, nm, holder) && holder != me) {
      out.detail = QStringLiteral("name is taken");
      return true;
    }
    const uint8_t rc = cc.registerKeyName(toBytes(nm));
    if (rc == ces::CES_OK) {
      setPreferredName(nm);
      out.ok = true;
      out.detail = QStringLiteral("registered %1").arg(nm);
    } else if (rc == ces::CES_ERROR_KEYNAME_TAKEN) {
      out.detail = QStringLiteral("name is taken");
    } else if (rc == ces::CES_ERROR_INSUFFICIENT_BALANCE) {
      out.detail = QStringLiteral("fund your account first (mine some credits)");
    } else {
      out.detail = QStringLiteral("register failed: %1")
                       .arg(QString::fromUtf8(ces::errorString(rc)));
    }
    return true;
  } catch (...) {
    out.detail = QStringLiteral("server unreachable");
    return true;
  }
}

QString ensureNameOnServer(const QString& host, uint16_t mainPort) {
  const QString pref = canonicalKeyName(preferredName());
  if (pref.isEmpty() || host.isEmpty() || mainPort == 0) return {};
  try {
    const ces::KeyPair id = loadOrCreateIdentity();
    const ces::Hash me = id.getPublicKeyAsHash();
    const auto ep = ces::Resolver::resolveUdp(
        host.toStdString() + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &id, 3);
    auto& cc = sess.client();
    if (nameForKey(cc, me) == pref) return {};  // already ours here
    ces::Hash holder{};
    if (nameHolder(cc, pref, holder) && holder != me) return {};  // someone else
    if (cc.registerKeyName(toBytes(pref)) == ces::CES_OK)
      return QStringLiteral("registered %1 on %2").arg(pref, host);
  } catch (...) {
  }
  return {};
}

bool maintainName(const QString& host, uint16_t mainPort, NameStatus& out) {
  out = {};
  const QString pref = canonicalKeyName(preferredName());
  out.server = host + ":" + QString::number(mainPort);
  if (pref.isEmpty()) {
    out.detail = QStringLiteral("no name set");
    return true;
  }
  out.name = pref;
  out.handle = pref;
  try {
    const ces::KeyPair id = loadOrCreateIdentity();
    const ces::Hash me = id.getPublicKeyAsHash();
    const auto ep = ces::Resolver::resolveUdp(
        host.toStdString() + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &id, 3);
    auto& cc = sess.client();

    QStringList did;
    const QString have = nameForKey(cc, me);
    if (have == pref) {
      out.ok = true;
    } else {
      ces::Hash holder{};
      if (nameHolder(cc, pref, holder) && holder != me) {
        did << QStringLiteral("NAME TAKEN by another key: pick a new one");
      } else if (cc.registerKeyName(toBytes(pref)) == ces::CES_OK) {
        out.ok = true;
        did << QStringLiteral("registered %1").arg(pref);
      } else {
        did << QStringLiteral("could not register (fund your account)");
      }
    }

    // Story upkeep: the browser does not let your posts die. Walk the shelf
    // (the worktree's /f/<handle>/ subtree), STAT each story on the server's
    // rpc lane, and feed any file whose rent runs low; RESTORE a missing one
    // from the local artifact. Signed by the one key that owns the zone.
    uint16_t rpcPort = 0;
    if (cc.getInfo()) rpcPort = cc.getServerRpcPort();
    if (out.ok && rpcPort != 0) {
      const QString msg = syncWorksToServer(host, rpcPort);
      if (!msg.isEmpty()) did << msg;
    }
    out.detail = did.isEmpty() ? QStringLiteral("healthy") : did.join(", ");
    return true;
  } catch (...) {
    out.detail = QStringLiteral("server unreachable");
    return true;
  }
}

QString syncWorksToServer(const QString& host, uint16_t rpcPort) {
  const ces::KeyPair id = loadOrCreateIdentity();
  const QString handle = canonicalKeyName(preferredName());
  if (handle.isEmpty() || host.isEmpty() || rpcPort == 0) return {};
  const QString zonePrefix = QStringLiteral("/f/%1/").arg(handle);
  const QStringList works = workList(zonePrefix);
  if (works.isEmpty()) return {};
  try {
    constexpr int64_t kLow = 500'000;      // feed under 0.005 credits
    constexpr uint64_t kFeed = 2'000'000;  // top up by 0.02 credits
    ces::CesFileClient fc;
    if (fc.connect(host.toStdString(), rpcPort, id) != ces::CES_OK) return {};
    int checked = 0, fed = 0, restored = 0;
    for (const QString& zp : works) {
      if (workRetired(zp)) continue;  // deliberately unpublished: never revive
      const std::string path = zp.toStdString();
      ces::CesFileClient::StatInfo st{};
      if (fc.stat(path, st) == ces::CES_OK) {
        ++checked;
        if (st.fileBalance < static_cast<uint64_t>(kLow)) {
          uint64_t nb = 0;
          if (fc.deposit(path, kFeed, nb) == ces::CES_OK) ++fed;
        }
        continue;
      }
      const QByteArray art = workArtifact(zp);
      if (art.isEmpty()) continue;
      uint64_t fb = 0, cost = 0;
      uint8_t rc = ces::CES_ERROR_INSUFFICIENT_BALANCE;
      for (uint64_t dep : {2'000'000ull, 200'000ull, 20'000ull}) {
        rc = fc.create(path, static_cast<uint64_t>(art.size()), 0, dep, fb,
                       cost);
        if (rc != ces::CES_ERROR_INSUFFICIENT_BALANCE) break;
      }
      if (rc == ces::CES_OK) {
        ces::Bytes bytes(art.begin(), art.end());
        uint64_t wb = 0;
        if (fc.write(path, 0, bytes, wb) == ces::CES_OK) ++restored;
      }
    }
    fc.disconnect();
    if (fed == 0 && restored == 0) return {};
    return QStringLiteral("stories on %1: %2 checked, %3 fed, %4 restored")
        .arg(host)
        .arg(checked)
        .arg(fed)
        .arg(restored);
  } catch (...) {
    return {};
  }
}

QString resolveName(const QString& host, uint16_t mainPort,
                    const QString& pubkeyHex) {
  if (pubkeyHex.size() != 64) return {};
  try {
    const auto ep = ces::Resolver::resolveUdp(
        host.toStdString() + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
    ces::Hash key{};
    minx::stringToHash(key, pubkeyHex.toStdString());
    return nameForKey(sess.client(), key);
  } catch (...) {
    return {};
  }
}

qint64 queryOwnBalance(const QString& host, uint16_t mainPort) {
  if (host.isEmpty() || mainPort == 0) return -1;
  try {
    const ces::KeyPair id = loadOrCreateIdentity();
    const auto ep = ces::Resolver::resolveUdp(
        host.toStdString() + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
    int64_t bal = 0;
    uint32_t nonce = 0;
    const uint8_t rc = sess.client().queryAccount(
        ces::Account::getMapKey(id.getPublicKeyAsHash()), bal, nonce);
    if (rc == ces::CES_OK)
      return static_cast<qint64>(bal);
    // A never-seen key has no ledger row yet. For our own identity that is not
    // an unknown balance: it is exactly the zero-credit state that must trigger
    // first-run mining. The first accepted proof creates/funds the account.
    if (rc == ces::CES_ERROR_ORIGIN_NOT_FOUND) return 0;
  } catch (...) {
  }
  return -1;
}

}  // namespace cwb
