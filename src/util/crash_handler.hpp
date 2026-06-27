#pragma once

namespace tgdb {

void install_crash_handlers();
void print_current_backtrace(const char* reason);

}  // namespace tgdb
