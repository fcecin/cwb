#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

// The local work tree: the PRIMARY copy of everything the user produces.
// Git's model: this tree is the repository, servers are remotes. A zone path
// is host-independent, so one local path names the same content everywhere.
// On-disk (content + sidecar, mirroring the CES store):
//   <appdata>/work/<zonePath>            the artifact
//   <appdata>/work/<zonePath>.src.txt    the canonical source
//   <appdata>/work/<zonePath>.work.toml  title, date, remotes, retired
namespace cwb {

// <appdata>/work (created on demand).
QString workRoot();

// Absolute local path for a zone path. Rejects traversal; "" on a bad path.
QString workLocalPath(const QString& zonePath);

// Save artifact + source, overwriting (one slug, one work). Keeps remotes;
// clears retired.
bool workSave(const QString& zonePath, const QByteArray& artifact,
              const QByteArray& source, const QString& title);

// Record that this work is published on host:port (idempotent).
void workNoteRemote(const QString& zonePath, const QString& hostPort);
QStringList workRemotes(const QString& zonePath);

// Deliberately unpublished: the work stays local, but upkeep must not push,
// restore, or feed it on any server. Cleared by the next workSave (publishing
// again is the un-retire gesture).
void workSetRetired(const QString& zonePath, bool retired);
bool workRetired(const QString& zonePath);

// Remove the work entirely: artifact, source, and provenance sidecar. True
// when none of the three remain.
bool workDelete(const QString& zonePath);

// The stored artifact bytes ("" if absent) -- the restore payload.
QByteArray workArtifact(const QString& zonePath);

// Zone paths under a prefix; artifact files only.
QStringList workList(const QString& zonePrefix);

// The work under `zonePrefix` whose title matches, or "". An edited story
// keeps its file name.
QString workFindByTitle(const QString& zonePrefix, const QString& title);

// The recorded title of a work ("" if none).
QString workTitle(const QString& zonePath);

// Crash-safe unpublished drafts, keyed by editing context; atomic replace.
bool draftSave(const QString& key, const QString& title, const QString& body);
bool draftLoad(const QString& key, QString& title, QString& body);
bool draftRemove(const QString& key);

}  // namespace cwb
