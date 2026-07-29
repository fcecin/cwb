#include "filekind.h"

#include <QLatin1String>

namespace {
struct Entry {
  const char* ext;
  const char* type;
  FileKind kind;
};

// Ported from cesweb/src/mime.js. Renderable-as-html -> Html; textual -> Text;
// source -> Code; tabular -> Csv; images -> Image; audio/video -> Media;
// everything else (archives, fonts, docs) -> Download.
const Entry kTable[] = {
    {"html", "text/html; charset=utf-8", FileKind::Html},
    {"htm", "text/html; charset=utf-8", FileKind::Html},
    {"css", "text/css; charset=utf-8", FileKind::Code},
    {"js", "text/javascript; charset=utf-8", FileKind::Code},
    {"mjs", "text/javascript; charset=utf-8", FileKind::Code},
    {"json", "application/json; charset=utf-8", FileKind::Json},
    {"txt", "text/plain; charset=utf-8", FileKind::Text},
    {"md", "text/markdown; charset=utf-8", FileKind::Markdown},
    {"markdown", "text/markdown; charset=utf-8", FileKind::Markdown},
    {"lua", "text/x-lua; charset=utf-8", FileKind::Code},
    {"c", "text/x-c; charset=utf-8", FileKind::Code},
    {"cpp", "text/x-c++; charset=utf-8", FileKind::Code},
    {"cc", "text/x-c++; charset=utf-8", FileKind::Code},
    {"cxx", "text/x-c++; charset=utf-8", FileKind::Code},
    {"h", "text/x-c; charset=utf-8", FileKind::Code},
    {"hpp", "text/x-c++; charset=utf-8", FileKind::Code},
    {"hh", "text/x-c++; charset=utf-8", FileKind::Code},
    {"py", "text/x-python; charset=utf-8", FileKind::Code},
    {"sh", "application/x-sh; charset=utf-8", FileKind::Code},
    {"bash", "application/x-sh; charset=utf-8", FileKind::Code},
    {"xml", "application/xml; charset=utf-8", FileKind::Text},
    {"csv", "text/csv; charset=utf-8", FileKind::Csv},
    {"tsv", "text/tab-separated-values; charset=utf-8", FileKind::Csv},
    {"svg", "image/svg+xml", FileKind::Image},
    {"png", "image/png", FileKind::Image},
    {"jpg", "image/jpeg", FileKind::Image},
    {"jpeg", "image/jpeg", FileKind::Image},
    {"gif", "image/gif", FileKind::Image},
    {"webp", "image/webp", FileKind::Image},
    {"avif", "image/avif", FileKind::Image},
    {"ico", "image/x-icon", FileKind::Image},
    {"bmp", "image/bmp", FileKind::Image},
    {"pdf", "application/pdf", FileKind::Download},
    {"wasm", "application/wasm", FileKind::Download},
    {"woff", "font/woff", FileKind::Download},
    {"woff2", "font/woff2", FileKind::Download},
    {"ttf", "font/ttf", FileKind::Download},
    {"otf", "font/otf", FileKind::Download},
    {"mp3", "audio/mpeg", FileKind::Media},
    {"ogg", "audio/ogg", FileKind::Media},
    {"wav", "audio/wav", FileKind::Media},
    {"m4a", "audio/mp4", FileKind::Media},
    {"opus", "audio/opus", FileKind::Media},
    {"flac", "audio/flac", FileKind::Media},
    {"aac", "audio/aac", FileKind::Media},
    {"mp4", "video/mp4", FileKind::Media},
    {"webm", "video/webm", FileKind::Media},
    {"mkv", "video/x-matroska", FileKind::Media},
    {"mov", "video/quicktime", FileKind::Media},
    {"zip", "application/zip", FileKind::Download},
    {"gz", "application/gzip", FileKind::Download},
};
}  // namespace

FileType fileTypeFor(const QString& path) {
  const int dot = path.lastIndexOf('.');
  const int slash = path.lastIndexOf('/');
  // No extension (or the dot is in a directory name) -> unknown -> download.
  if (dot < 0 || dot < slash)
    return {FileKind::Download, QStringLiteral("application/octet-stream")};
  const QString ext = path.mid(dot + 1).toLower();
  for (const auto& e : kTable)
    if (ext == QLatin1String(e.ext)) return {e.kind, QString::fromUtf8(e.type)};
  return {FileKind::Download, QStringLiteral("application/octet-stream")};
}
