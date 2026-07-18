#pragma once

#include <string>
#include <vector>

namespace tuide {

// Minimal read-only line-indexed text abstraction. This lets code that only
// ever needs `size()`/`at(i)` (e.g. syntax highlighting) work uniformly
// whether the backing storage is a plain std::vector<std::string> (e.g. a
// read-only source preview panel) or an EditorText (the editable buffer,
// which may be backed by a rope -- see editor_text.hpp).
class LineSource {
 public:
  virtual ~LineSource() = default;
  virtual int size() const = 0;
  virtual const std::string& at(int index) const = 0;
};

class VectorLineSource : public LineSource {
 public:
  explicit VectorLineSource(const std::vector<std::string>& lines) : lines_(&lines) {}

  int size() const override { return static_cast<int>(lines_->size()); }
  const std::string& at(int index) const override {
    return (*lines_)[static_cast<std::size_t>(index)];
  }

 private:
  const std::vector<std::string>* lines_;
};

}  // namespace tuide
