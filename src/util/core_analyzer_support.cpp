#include "util/core_analyzer_support.hpp"

namespace tgdb {

bool core_analyzer_supported() {
#ifdef TGDB_BUNDLED_GDB_KIND_CORE_ANALYZER
  return true;
#elif defined(TGDB_BUNDLED_GDB_KIND_STATIC)
  return false;
#else
  return true;
#endif
}

}  // namespace tgdb
