#include "ui/focusable_component.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"

namespace tgdb {

using namespace ftxui;

Component WrapFocusable(Component child) {
  class FocusableWrapper : public ComponentBase {
   public:
    explicit FocusableWrapper(Component inner) { Add(std::move(inner)); }

    bool Focusable() const override { return true; }
  };

  return Make<FocusableWrapper>(std::move(child));
}

}  // namespace tgdb
