#pragma once
#include <QString>

#include <cstdint>

// The name engine over CES key_names (ces::KeyNames): the name is a crypto-owned
// ledger cell keyed by our ONE public key, unique both ways, rent-paid by our
// account. Registering IS the identity: whoever owns the name owns the
// /f/<handle>/ zone (the file store's A' gate). No asset triangle, no second
// key, no round-trip verification -- the ledger enforces bidirectional
// uniqueness natively. All functions are blocking (own their networking) --
// call from a worker thread or a CLI verb, never the GUI thread.
namespace cwb {

struct NameStatus {
  bool ok = false;     // the name is registered to our key on the server
  QString name;        // the display name ("" if none)
  QString handle;      // the canonical name (== /f/ segment, underscore form)
  QString server;      // host:mainport acted on
  QString detail;      // human summary
};

// Register (or confirm) `name` for our key on host:mainPort. Validates it; if
// our key already holds it there, done; if another key holds it, fails "name
// taken"; else submits CES_REGISTER_KEYNAME (our account must be funded on that
// server). Persists it as the preferred name. `out` filled either way.
bool registerName(const QString& host, uint16_t mainPort, const QString& name,
                  NameStatus& out);

// Auto-on-entry: ensure our preferred name is registered on host:mainPort.
// No-op (returns "") if we have no preferred name, if it is already ours there,
// or if it is taken by someone else. Best-effort. Returns a short summary of
// what it did.
QString ensureNameOnServer(const QString& host, uint16_t mainPort);

// Boot upkeep on host:mainPort: confirm our name, then feed/restore our
// published stories from the local worktree. Best-effort. `out` filled.
bool maintainName(const QString& host, uint16_t mainPort, NameStatus& out);

// Mirror-to-current-server: make host:rpcPort carry every work under our
// /f/<handle>/ subtree -- feed the hungry, RESTORE the missing from the local
// worktree (the worktree is the truth). Best-effort; returns a short human
// summary ("" = nothing to do).
QString syncWorksToServer(const QString& host, uint16_t rpcPort);

// The name registered to a pubkey on a server (ledger-verified, unsquattable by
// construction). "" if unnamed. Also the CLI `resolvename`.
QString resolveName(const QString& host, uint16_t mainPort,
                    const QString& pubkeyHex);

// Our single account's balance on host:mainPort (internal units; -1 unknown).
qint64 queryOwnBalance(const QString& host, uint16_t mainPort);

}  // namespace cwb
