#pragma once
#include <QChar>
#include <QString>

// Render CSV/TSV text as a styled HTML table (first row = header). RFC-4180-ish:
// fields split on `sep`, double-quoted fields may contain the separator and
// newlines, "" is a literal quote, CRLF is tolerated. Widgets-free (cwb_core) so
// it is unit-tested. No JavaScript, ever.
namespace cwb {

// Full styled HTML page with the parsed table. `sep` is ',' for csv, '\t' tsv.
QString csvToHtmlPage(const QString& text, QChar sep = QLatin1Char(','));

}  // namespace cwb
