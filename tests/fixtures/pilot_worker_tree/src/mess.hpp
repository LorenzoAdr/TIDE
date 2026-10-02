#pragma once

namespace demo {

struct State {
  bool armed = false;
  bool busy = false;
  bool halted = false;
  int ticks = 0;
};

void arm(State* s);
void set_busy(State* s);
void tick(State* s);
void keep_alive(State* s);
void cousin(State* s);
void clear_busy(State* s);
void halt(State* s);

}  // namespace demo
