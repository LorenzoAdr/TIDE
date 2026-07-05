#include "util/crash_handler.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

#include "i18n/tr.hpp"

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <unistd.h>
#endif

namespace tgdb {

namespace {

#if defined(__linux__) || defined(__APPLE__)
void print_backtrace_to_stderr(const char* reason) {
  void* frames[128];
  const int count = backtrace(frames, 128);
  char** symbols = backtrace_symbols(frames, count);

  std::cerr << i18n::tr_fmt("crash.backtrace_header", {reason});
  if (symbols != nullptr) {
    for (int i = 0; i < count; ++i) {
      std::cerr << "  #" << i << " " << symbols[i] << '\n';
    }
    std::free(symbols);
  } else {
    std::cerr << i18n::tr("crash.symbols_unresolved");
  }
  std::cerr << std::flush;
}

void on_fatal_signal(int sig) {
  const char* name = "signal";
  switch (sig) {
    case SIGSEGV:
      name = "SIGSEGV";
      break;
    case SIGABRT:
      name = "SIGABRT";
      break;
    case SIGFPE:
      name = "SIGFPE";
      break;
    case SIGILL:
      name = "SIGILL";
      break;
    case SIGBUS:
      name = "SIGBUS";
      break;
    default:
      break;
  }
  print_backtrace_to_stderr(name);
  _exit(128 + sig);
}
#endif

}  // namespace

void install_crash_handlers() {
  std::set_terminate([] {
    print_current_backtrace("std::terminate");
    std::abort();
  });
#if defined(__linux__) || defined(__APPLE__)
  std::signal(SIGSEGV, on_fatal_signal);
  std::signal(SIGABRT, on_fatal_signal);
  std::signal(SIGFPE, on_fatal_signal);
  std::signal(SIGILL, on_fatal_signal);
  std::signal(SIGBUS, on_fatal_signal);
#endif
}

void print_current_backtrace(const char* reason) {
#if defined(__linux__) || defined(__APPLE__)
  print_backtrace_to_stderr(reason);
#else
  std::cerr << i18n::tr_fmt("crash.unavailable", {reason});
#endif
}

}  // namespace tgdb
