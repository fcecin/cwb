#pragma once
#include <ces/keys.h>

#include <QString>

// The browser's ONE key (identity.key), persisted in the app data dir. It is
// the mining bank, the money, AND the authorship identity: one key, one
// account, one public-key address. Your NAME is a key_name registered on each
// server you use (see identityreg / ces::KeyNames); the PREFERRED name is
// remembered locally so the browser can auto-register it as you move between
// servers. No profiles, no second key: a cwb installation is one person.
namespace cwb {

// The single key. Generated on first use.
ces::KeyPair loadOrCreateIdentity();
// The authorship identity == the single key. Kept as a distinct call so widgets
// reading it stay legible.
ces::KeyPair authorIdentity();

// The preferred name to register everywhere ("" = none yet). The display name
// (interior spaces allowed); the /f/ handle is its normalized form.
QString preferredName();
void setPreferredName(const QString& name);

}  // namespace cwb
