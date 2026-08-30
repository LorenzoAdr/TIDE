namespace demo {

struct Session {
  bool busy = false;
  int ticks = 0;
};

void start_busy(Session* s) {
  if (s == nullptr) {
    return;
  }
  s->busy = true;
  s->ticks = 0;
}

void tick(Session* s) {
  if (s == nullptr || !s->busy) {
    return;
  }
  s->ticks += 1;
}

void halt_busy(Session* s) {
  if (s == nullptr) {
    return;
  }
  s->busy = false;
}

}  // namespace demo
