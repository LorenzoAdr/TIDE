#pragma once

#include <string>

// Stub retained for local debug sessions; instrumentation is disabled in release builds.
namespace tgdb::debug_session {

inline void trace(const char*, const char*, const char*, const std::string& = {}) {}
inline std::string json_escape(const std::string& s) { return s; }

}  // namespace tgdb::debug_session
