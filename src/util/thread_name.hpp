#pragma once

#ifdef __linux__
#include <pthread.h>
#endif

namespace tuide {

inline void set_current_thread_name(const char* name) {
#ifdef __linux__
  if (name != nullptr) {
    pthread_setname_np(pthread_self(), name);
  }
#else
  (void)name;
#endif
}

}  // namespace tuide
