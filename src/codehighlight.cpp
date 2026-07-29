#include "codehighlight.h"

#include <QSet>
#include <QStringList>

namespace cwb {
namespace {

struct LangSpec {
  QString lineComment;   // e.g. "//", "--", "#"; empty = none
  bool blockComment;     // C-style /* ... */
  QSet<QString> keywords;
};

QSet<QString> words(const char* s) {
  const QStringList list = QString::fromLatin1(s).split(' ');
  return QSet<QString>(list.begin(), list.end());
}

LangSpec specFor(const QString& ext) {
  const QString e = ext.toLower();
  static const QSet<QString> lua = words(
      "and break do else elseif end false for function goto if in local nil not "
      "or repeat return then true until while self");
  static const QSet<QString> js = words(
      "var let const function return if else for while do switch case break "
      "continue new delete typeof instanceof this class extends super import "
      "export default from await async yield try catch finally throw true false "
      "null undefined void in of");
  static const QSet<QString> c = words(
      "auto break case char const continue default do double else enum extern "
      "float for goto if inline int long register return short signed sizeof "
      "static struct switch typedef union unsigned void volatile while bool true "
      "false class namespace template public private protected virtual override "
      "new delete this nullptr using constexpr auto");
  static const QSet<QString> py = words(
      "def class return if elif else for while break continue import from as "
      "pass lambda try except finally raise with yield global nonlocal in is not "
      "and or None True False del assert async await");
  static const QSet<QString> sh = words(
      "if then else elif fi for while do done case esac in function select "
      "until return break continue local export readonly declare echo");

  if (e == "lua") return {"--", false, lua};
  if (e == "js" || e == "mjs") return {"//", true, js};
  if (e == "c" || e == "cpp" || e == "cc" || e == "cxx" || e == "h" ||
      e == "hpp" || e == "hh")
    return {"//", true, c};
  if (e == "py") return {"#", false, py};
  if (e == "sh" || e == "bash") return {"#", false, sh};
  if (e == "css") return {QString(), true, {}};
  return {QString(), false, {}};  // unknown: strings/numbers only
}

QString esc(QChar c) {
  if (c == '&') return QStringLiteral("&amp;");
  if (c == '<') return QStringLiteral("&lt;");
  if (c == '>') return QStringLiteral("&gt;");
  return QString(c);
}

}  // namespace

QString highlightCode(const QString& src, const QString& ext) {
  const LangSpec spec = specFor(ext);
  const int n = src.size();
  QString out;
  out.reserve(n + n / 4);
  const auto isIdentStart = [](QChar c) { return c.isLetter() || c == '_'; };
  const auto isIdent = [](QChar c) { return c.isLetterOrNumber() || c == '_'; };

  int i = 0;
  while (i < n) {
    const QChar c = src[i];
    // block comment /* ... */
    if (spec.blockComment && c == '/' && i + 1 < n && src[i + 1] == '*') {
      int j = i + 2;
      while (j < n && !(src[j] == '*' && j + 1 < n && src[j + 1] == '/')) ++j;
      const int end = (j < n) ? j + 2 : n;
      out += QStringLiteral("<span class=cm>");
      for (int k = i; k < end; ++k) out += esc(src[k]);
      out += QStringLiteral("</span>");
      i = end;
      continue;
    }
    // line comment
    if (!spec.lineComment.isEmpty() &&
        src.mid(i, spec.lineComment.size()) == spec.lineComment) {
      int j = i;
      while (j < n && src[j] != '\n') ++j;
      out += QStringLiteral("<span class=cm>");
      for (int k = i; k < j; ++k) out += esc(src[k]);
      out += QStringLiteral("</span>");
      i = j;
      continue;
    }
    // string (double or single quoted, backslash escapes, unterminated at EOL)
    if (c == '"' || c == '\'') {
      const QChar q = c;
      int j = i + 1;
      while (j < n) {
        if (src[j] == '\\' && j + 1 < n) {
          j += 2;
          continue;
        }
        if (src[j] == q) {
          ++j;
          break;
        }
        if (src[j] == '\n') break;
        ++j;
      }
      out += QStringLiteral("<span class=st>");
      for (int k = i; k < j; ++k) out += esc(src[k]);
      out += QStringLiteral("</span>");
      i = j;
      continue;
    }
    // number
    if (c.isDigit()) {
      int j = i;
      while (j < n && (src[j].isLetterOrNumber() || src[j] == '.')) ++j;
      out += QStringLiteral("<span class=nu>");
      for (int k = i; k < j; ++k) out += esc(src[k]);
      out += QStringLiteral("</span>");
      i = j;
      continue;
    }
    // identifier / keyword
    if (isIdentStart(c)) {
      int j = i;
      while (j < n && isIdent(src[j])) ++j;
      const QString word = src.mid(i, j - i);  // ident chars are HTML-safe
      if (spec.keywords.contains(word))
        out += QStringLiteral("<span class=kw>") + word + QStringLiteral("</span>");
      else
        out += word;
      i = j;
      continue;
    }
    out += esc(c);
    ++i;
  }
  return out;
}

QString codePageHtml(const QString& src, const QString& ext) {
  const int lines = src.count(QLatin1Char('\n')) + 1;
  QString gutter;
  gutter.reserve(lines * 4);
  for (int i = 1; i <= lines; ++i) {
    if (i > 1) gutter += QLatin1Char('\n');
    gutter += QString::number(i);
  }
  // Two-column table (line-number gutter + code) so numbers line up with lines
  // without splitting the highlighted HTML (which can span lines in a block
  // comment). Both <pre>s share font/line-height; the code uses white-space:pre
  // (no wrap) to keep the 1:1 line alignment.
  return QStringLiteral(
             "<!doctype html><html><head><meta charset=utf-8><style>"
             "body{margin:0;background:#fbfbfb}table.cv{border-collapse:collapse}"
             "td{vertical-align:top;padding:0}"
             "pre{margin:0;font-family:monospace;font-size:13px;line-height:1.5}"
             "td.gut pre{padding:16px 10px;text-align:right;color:#b0b6be;"
             "background:#f3f4f6;border-right:1px solid #e2e6ea}"
             "td.src pre{padding:16px;white-space:pre;color:#1a1d22}"
             ".kw{color:#0033b3;font-weight:600}.st{color:#067d17}"
             ".cm{color:#8c8c8c;font-style:italic}.nu{color:#1750eb}"
             "</style></head><body><table class=cv><tr>"
             "<td class=gut><pre>%1</pre></td>"
             "<td class=src><pre>%2</pre></td>"
             "</tr></table></body></html>")
      .arg(gutter, highlightCode(src, ext));
}

}  // namespace cwb
