#include "util/child_process_guard.hpp"

#include <csignal>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace tuide {

void child_die_with_parent(const int signal) {
#if defined(__linux__)
  prctl(PR_SET_PDEATHSIG, signal);
  // Parent died in the fork/exec race window.
  if (getppid() == 1) {
    raise(signal);
  }
#else
  (void)signal;
#endif
}

}  // namespace tuide
