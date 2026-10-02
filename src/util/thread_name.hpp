#pragma once

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace tuide {

inline void set_current_thread_name(const char* name) {
#if defined(__linux__)
  if (name != nullptr) {
    pthread_setname_np(pthread_self(), name);
  }
#elif defined(__APPLE__)
  if (name != nullptr) {
    pthread_setname_np(name);
  }
#else
  (void)name;
#endif
}

}  // namespace tuide
