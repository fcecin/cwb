#include "csvtable.h"

#include <QList>
#include <QStringList>

namespace cwb {
namespace {

// Parse CSV/TSV into rows of fields, honoring quoted fields (which may contain
// the separator, quotes as "", and embedded newlines).
QList<QStringList> parseCsv(const QString& s, QChar sep) {
  QList<QStringList> rows;
  QStringList row;
  QString field;
  bool inQuotes = false;
  bool sawAny = false;  // any character seen on the current (pending) row
  const int n = s.size();
  const auto endField = [&]() {
    row << field;
    field.clear();
  };
  const auto endRow = [&]() {
    endField();
    rows << row;
    row.clear();
    sawAny = false;
  };
  for (int i = 0; i < n; ++i) {
    const QChar c = s[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < n && s[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          inQuotes = false;
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      inQuotes = true;
      sawAny = true;
    } else if (c == sep) {
      endField();
      sawAny = true;
    } else if (c == '\n') {
      endRow();
    } else if (c == '\r') {
      // tolerate CRLF: ignore the CR
    } else {
      field += c;
      sawAny = true;
    }
  }
  if (sawAny || !field.isEmpty() || !row.isEmpty()) endRow();  // final row
  return rows;
}

QString esc(const QString& in) {
  QString o = in;
  o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
  return o;
}

}  // namespace

QString csvToHtmlPage(const QString& text, QChar sep) {
  const QList<QStringList> rows = parseCsv(text, sep);
  QString body;
  if (rows.isEmpty()) {
    body = QStringLiteral("<p class=empty>(empty)</p>");
  } else {
    body = QStringLiteral("<table><thead><tr>");
    for (const QString& h : rows.first())
      body += QStringLiteral("<th>") + esc(h) + QStringLiteral("</th>");
    body += QStringLiteral("</tr></thead><tbody>");
    for (int r = 1; r < rows.size(); ++r) {
      body += QStringLiteral("<tr>");
      for (const QString& cell : rows[r])
        body += QStringLiteral("<td>") + esc(cell) + QStringLiteral("</td>");
      body += QStringLiteral("</tr>");
    }
    body += QStringLiteral("</tbody></table>");
  }
  return QStringLiteral(
             "<!doctype html><html><head><meta charset=utf-8><style>"
             "body{font-family:sans-serif;margin:20px;color:#1a1d22}"
             "table{border-collapse:collapse;font-size:14px}"
             "th,td{border:1px solid #d0d7de;padding:6px 12px;text-align:left;"
             "vertical-align:top}"
             "th{background:#f2f4f7;position:sticky;top:0}"
             "tr:nth-child(even) td{background:#fafbfc}"
             ".empty{color:#888}"
             "</style></head><body>%1</body></html>")
      .arg(body);
}

}  // namespace cwb
