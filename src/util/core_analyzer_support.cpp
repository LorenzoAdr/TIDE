#include "util/core_analyzer_support.hpp"

namespace tuide {

bool core_analyzer_supported() {
#ifdef TUIDE_BUNDLED_GDB_KIND_CORE_ANALYZER
  return true;
#elif defined(TUIDE_BUNDLED_GDB_KIND_STATIC)
  return false;
#else
  return true;
#endif
}

}  // namespace tuide
