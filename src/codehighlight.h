#pragma once
#include <QString>

// Lightweight source syntax highlighter: turns code into HTML with colored
// <span> classes (kw/st/cm/nu) that litehtml then styles. A single generic
// tokenizer (strings, line/block comments, numbers, keywords) configured per
// language by file extension -- good enough to make source pages look like code
// without a full per-language grammar. Widgets-free (cwb_core) so it is
// unit-tested. No JavaScript, ever.
namespace cwb {

// Highlight `src` for the given lowercase-or-not file `ext` (e.g. "lua", "js").
// Returns an HTML fragment (already HTML-escaped) meant to sit inside a <pre>.
QString highlightCode(const QString& src, const QString& ext);

// A complete styled HTML page wrapping highlightCode() in a <pre> with the
// color theme. This is what the browser renders for a code file.
QString codePageHtml(const QString& src, const QString& ext);

}  // namespace cwb
