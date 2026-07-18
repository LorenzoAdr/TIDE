#pragma once

#include <csignal>

namespace tuide {

// Child-side: receive `signal` when the parent dies (Linux prctl PR_SET_PDEATHSIG).
void child_die_with_parent(int signal = SIGTERM);

}  // namespace tuide
