#include "terminalpane.h"

#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QTextCursor>

TerminalPane::TerminalPane(QWidget* parent) : QPlainTextEdit(parent) {
  setUndoRedoEnabled(false);
  setLineWrapMode(QPlainTextEdit::WidgetWidth);
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  f.setPointSize(11);
  setFont(f);
  setStyleSheet("QPlainTextEdit{background:#101418;color:#d6dbe3;"
                "selection-background-color:#2a3646;}");
  setFocusPolicy(Qt::StrongFocus);
}

void TerminalPane::cursorToEnd() {
  QTextCursor c = textCursor();
  c.movePosition(QTextCursor::End);
  setTextCursor(c);
}

void TerminalPane::appendBytes(const QString& text) {
  cursorToEnd();
  insertPlainText(text);
  cursorToEnd();
  ensureCursorVisible();
}

void TerminalPane::keyPressEvent(QKeyEvent* e) {
  const int k = e->key();
  if (k == Qt::Key_Return || k == Qt::Key_Enter) {
    appendBytes("\n");
    emit sendBytes(input_ + "\n");
    input_.clear();
    return;
  }
  if (k == Qt::Key_Backspace) {
    if (!input_.isEmpty()) {
      input_.chop(1);
      cursorToEnd();
      QTextCursor c = textCursor();
      c.deletePreviousChar();
      setTextCursor(c);
    }
    return;
  }
  // Swallow navigation/edit keys so the cursor can't roam into the scrollback.
  switch (k) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Delete:
      return;
    default:
      break;
  }
  const QString t = e->text();
  if (!t.isEmpty() && t[0].isPrint()) {
    input_ += t;
    appendBytes(t);  // local echo
  }
}
