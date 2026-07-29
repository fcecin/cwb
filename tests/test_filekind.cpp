#include "filekind.h"

#include <QtTest>

class TestFileKind : public QObject {
  Q_OBJECT
 private slots:
  void kinds_data();
  void kinds();
  void fallbacks();
};

void TestFileKind::kinds_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<int>("kind");
  QTest::newRow("html") << "/p/index.html" << int(FileKind::Html);
  QTest::newRow("htm") << "/p/x.htm" << int(FileKind::Html);
  QTest::newRow("txt") << "/p/readme.txt" << int(FileKind::Text);
  QTest::newRow("md") << "/p/doc.md" << int(FileKind::Markdown);
  QTest::newRow("markdown") << "/p/doc.markdown" << int(FileKind::Markdown);
  QTest::newRow("lua") << "/s/dice.lua" << int(FileKind::Code);
  QTest::newRow("js") << "/p/app.js" << int(FileKind::Code);
  QTest::newRow("cpp") << "/p/main.cpp" << int(FileKind::Code);
  QTest::newRow("py") << "/p/x.py" << int(FileKind::Code);
  QTest::newRow("css") << "/p/style.css" << int(FileKind::Code);
  QTest::newRow("json") << "/p/data.json" << int(FileKind::Json);
  QTest::newRow("csv") << "/p/rows.csv" << int(FileKind::Csv);
  QTest::newRow("tsv") << "/p/rows.tsv" << int(FileKind::Csv);
  QTest::newRow("png") << "/p/logo.png" << int(FileKind::Image);
  QTest::newRow("jpg") << "/p/photo.jpg" << int(FileKind::Image);
  QTest::newRow("gif") << "/p/anim.gif" << int(FileKind::Image);
  QTest::newRow("svg") << "/p/vector.svg" << int(FileKind::Image);
  QTest::newRow("ico") << "/p/favicon.ico" << int(FileKind::Image);
  QTest::newRow("pdf") << "/p/spec.pdf" << int(FileKind::Download);
  QTest::newRow("mp4") << "/p/clip.mp4" << int(FileKind::Media);
  QTest::newRow("mp3") << "/p/song.mp3" << int(FileKind::Media);
  QTest::newRow("zip") << "/p/a.zip" << int(FileKind::Download);
  QTest::newRow("case-insensitive") << "/p/INDEX.HTML" << int(FileKind::Html);
  QTest::newRow("png-upper") << "/p/LOGO.PNG" << int(FileKind::Image);
}

void TestFileKind::kinds() {
  QFETCH(QString, path);
  QFETCH(int, kind);
  QCOMPARE(int(fileTypeFor(path).kind), kind);
}

void TestFileKind::fallbacks() {
  // No extension -> download + octet-stream.
  QCOMPARE(int(fileTypeFor("/p/noext").kind), int(FileKind::Download));
  QCOMPARE(fileTypeFor("/p/noext").contentType,
           QStringLiteral("application/octet-stream"));
  // A dot only inside a directory name is not an extension.
  QCOMPARE(int(fileTypeFor("/p/dir.with.dot/file").kind),
           int(FileKind::Download));
  // A known type still carries its guessed MIME.
  QCOMPARE(fileTypeFor("/p/index.html").contentType,
           QStringLiteral("text/html; charset=utf-8"));
}

QTEST_MAIN(TestFileKind)
#include "test_filekind.moc"
