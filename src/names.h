#pragma once
#include <QString>

// The name is a key_name on the CES ledger (see ces/keynames.h): a crypto-owned
// 32-byte cell keyed by the pubkey, unique both ways, rent-paid. The DISPLAY
// name (interior spaces allowed) and the path HANDLE (spaces -> underscores)
// are one concept: the handle is just the name normalized, so "Ada Lovelace"
// and "/f/Ada_Lovelace/" are the same identity. These helpers mirror the
// server's KeyNames::validName / ::normalize so the client validates the same
// way the ledger will.
namespace cwb {

// Canonical key_name (mirrors the server's normalize + validName): spaces and
// underscores both map to '_', edges stripped, then validated. The underscore
// form IS the stored name and the /f/<name>/ segment. "" if not a valid name.
QString canonicalKeyName(const QString& raw);

// The pretty display form: underscores rendered back as spaces. Bylines and
// interfaces show this; URLs and paths use the underscore form. Lossy by design
// (a name is stored in the underscore form only).
QString prettyName(const QString& keyname);

// A story's file name, derived from its title + content (Medium-style):
// kebab-case title (lowercased alnum runs joined by '-', capped) + '-' +
// 6 hex chars of the source's hash + ".html". The name is fully derivable
// client-side: the widget names, the server routes nothing.
QString storyFileName(const QString& title, const QByteArray& source);

}  // namespace cwb
