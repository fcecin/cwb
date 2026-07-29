#pragma once
#include <QPlainTextEdit>
#include <QString>

// A minimal native terminal control. Append-only scrollback; a single input line
// pinned at the end; the cursor is kept out of history. Printable keys echo and
// buffer locally; Enter emits the line (with a trailing newline) and clears it.
// Line-oriented, which is what CES terminal programs (e.g. dice) speak.
class TerminalPane : public QPlainTextEdit {
  Q_OBJECT
 public:
  explicit TerminalPane(QWidget* parent = nullptr);

  // Output arriving from the connection: appended verbatim at the end.
  void appendBytes(const QString& text);

 signals:
  // A completed input line to send to the connection (includes the newline).
  void sendBytes(const QString& bytes);

 protected:
  void keyPressEvent(QKeyEvent* e) override;

 private:
  void cursorToEnd();
  QString input_;
};
