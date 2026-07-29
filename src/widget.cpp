#include "widget.h"

namespace cwb {

WidgetRegistry& WidgetRegistry::instance() {
  static WidgetRegistry r;
  return r;
}

void WidgetRegistry::add(const QString& type, WidgetFactory factory) {
  factories_[type] = std::move(factory);
}

bool WidgetRegistry::has(const QString& type) const {
  return factories_.find(type) != factories_.end();
}

QWidget* WidgetRegistry::create(const WidgetParams& p, WidgetContext* ctx,
                                QWidget* parent) const {
  auto it = factories_.find(p.type);
  return it == factories_.end() ? nullptr : it->second(p, ctx, parent);
}

QStringList WidgetRegistry::types() const {
  QStringList out;
  for (const auto& kv : factories_) out << kv.first;
  return out;
}

}  // namespace cwb
