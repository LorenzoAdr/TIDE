#pragma once

#include <memory>
#include <vector>

#include "terminal/shell_session.hpp"

namespace tuide {

// Owns zero-or-more ShellSession instances (terminal tabs) and tracks which
// one is active. Always keeps at least one session alive: closing the last
// remaining one immediately creates a fresh replacement, preserving the
// pre-existing invariant that a shell always exists.
class ShellSessionManager {
 public:
  ShellSessionManager();

  int count() const;
  int active_index() const;
  void set_active_index(int index);

  ShellSession* session_at(int index);
  ShellSession* active_session();

  // Creates a new session, makes it active, and returns its index.
  int add_session();

  // Stops and removes the session at `index`. If it was the last one, a
  // fresh replacement is created so there is always at least one session.
  void close_session(int index);

  void move_session(int from, int to);

  // Stops every session and returns to a single fresh (not-yet-started)
  // session. Used when the workspace changes.
  void reset();

  // Stops every session (app shutdown).
  void stop_all();

 private:
  std::vector<std::unique_ptr<ShellSession>> sessions_;
  int active_ = 0;
};

}  // namespace tuide
