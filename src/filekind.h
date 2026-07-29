#pragma once
#include <QString>

// CES stores no content-type: a file is a name with an extension, nothing more.
// So the browser guesses from the extension what to DO with the bytes:
//   Html     -> render as an HTML page (litehtml)
//   Text     -> render as readable plain text (no JS, ever)
//   Markdown -> convert Markdown to HTML (md4c) and render it styled
//   Code     -> syntax-highlighted source view (lua/js/c/py/sh/css...)
//   Csv      -> rendered as an HTML table (csv/tsv)
//   Json     -> pretty-print (indented) in a monospace view; raw text if invalid
//   Image    -> display in the inline image viewer (raster via QImage, svg crisp
//               via QSvgRenderer); falls back to a save dialog if it won't decode
//   Media    -> play in the in-window audio/video player; download on failure
//   Download -> save to disk; cwb is a minimal renderer and won't display it
// The extension->type map is ported from cesweb/src/mime.js so the gateway and
// the native browser agree on what a given name means.
enum class FileKind { Html, Text, Markdown, Code, Csv, Json, Image, Media, Download };

struct FileType {
  FileKind kind;
  QString contentType;  // guessed MIME (informational; octet-stream fallback)
};

FileType fileTypeFor(const QString& path);
