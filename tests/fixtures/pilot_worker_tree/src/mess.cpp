#include "mess.hpp"

namespace demo {

void arm(State* s) {
  if (s == nullptr) {
    return;
  }
  s->armed = true;
}

void set_busy(State* s) {
  if (s == nullptr) {
    return;
  }
  s->busy = true;
  s->halted = false;
  keep_alive(s);
}

void tick(State* s) {
  if (s == nullptr || !s->busy) {
    return;
  }
  s->ticks += 1;
}

void keep_alive(State* s) {
  if (s == nullptr) {
    return;
  }
  if (s->busy) {
    s->ticks += 0;
  }
}

void cousin(State* s) {
  if (s == nullptr) {
    return;
  }
  s->armed = false;
}

void clear_busy(State* s) {
  if (s == nullptr) {
    return;
  }
  s->busy = false;
}

void halt(State* s) {
  if (s == nullptr) {
    return;
  }
  s->halted = true;
  s->busy = false;
}

}  // namespace demo
