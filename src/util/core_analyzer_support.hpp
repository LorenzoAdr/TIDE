#pragma once

namespace tgdb {

// True when this build supports Core Analyzer (CA bundle, or gdb from PATH).
bool core_analyzer_supported();

}  // namespace tgdb
