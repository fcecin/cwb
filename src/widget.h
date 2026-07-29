#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>
#include <ces/keys.h>

#include <functional>
#include <map>

// Native embedded widgets. A page embeds a compiled-in widget with
// <object type="TYPE"><param ...></object>; the page carries no code, only
// params. Drop a .cpp in src/widgets/ (CMake globs); it self-registers.
namespace cwb {

// A fullscreen application page: the sole content of a navigation. The
// browser keeps chrome/history/guards/scrolling; the widget owns the canvas
// and reports content-height changes.
class FullscreenWidget : public QWidget {
  Q_OBJECT
 public:
  using QWidget::QWidget;

  // Chrome pinned to the browser viewport (y=0) while the page scrolls under
  // it; the page must reserve the same height in its own flow.
  void setViewportChrome(QWidget* chrome) { viewportChrome_ = chrome; }
  QWidget* viewportChrome() const { return viewportChrome_; }

 signals:
  void contentSizeChanged();

 private:
  QWidget* viewportChrome_ = nullptr;
};

// Browser capabilities a widget may use: the one identity that signs CES ops,
// the page's server, and navigation. Grown as widgets need it.
class WidgetContext {
 public:
  virtual ~WidgetContext() = default;
  // The ONE key: mining bank, money, and authorship, one account.
  virtual ces::KeyPair identity() const = 0;
  // == identity(); a distinct call for legibility at content sites.
  virtual ces::KeyPair authorIdentity() const = 0;
  // Display name ("" if none): the byline, the registered key_name.
  virtual QString authorName() const = 0;
  // Handle ("" if none): the normalized name, the /f/<handle>/ segment.
  virtual QString authorHandle() const = 0;
  virtual QString serverHost() const = 0;
  virtual quint16 serverRpcPort() const = 0;
  virtual QString currentAddress() const = 0;
  virtual void navigateTo(const QString& url) = 0;
  // Re-render the current page after an action changed what it would show.
  virtual void reloadCurrent() = 0;
};

struct WidgetParams {
  QString type;                       // the <object> type attribute
  QString data;                       // the data=/src= URL (a CES address)
  std::map<QString, QString> params;  // <param name= value=> pairs
};

// A factory turns an embed into a native QWidget to overlay in the page.
using WidgetFactory = std::function<QWidget*(const WidgetParams&, WidgetContext*,
                                             QWidget* parent)>;

// Registry of embeddable native widgets, keyed by TYPE. Widgets self-register at
// static-init via WidgetRegistrar; there is no central list to edit.
class WidgetRegistry {
 public:
  static WidgetRegistry& instance();
  void add(const QString& type, WidgetFactory factory);
  bool has(const QString& type) const;
  QWidget* create(const WidgetParams& p, WidgetContext* ctx,
                  QWidget* parent) const;
  QStringList types() const;

 private:
  std::map<QString, WidgetFactory> factories_;
};

// One per widget .cpp:
//   static cwb::WidgetRegistrar reg("application/x-cwb-pay", &makePayWidget);
struct WidgetRegistrar {
  WidgetRegistrar(const QString& type, WidgetFactory factory) {
    WidgetRegistry::instance().add(type, std::move(factory));
  }
};

}  // namespace cwb
