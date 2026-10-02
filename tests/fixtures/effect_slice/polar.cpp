namespace tuide {
namespace effect_slice_fix {

struct Box {
  int flag = 0;
  int noise = 0;
};

void later(Box* b) {
  if (b == nullptr) {
    return;
  }
  b->flag = 0;
}

void set_x(Box* b) {
  if (b == nullptr) {
    return;
  }
  b->flag = 1;
}

void clear_x(Box* b) {
  if (b == nullptr) {
    return;
  }
  if (b->flag == 1) {
    b->flag = 0;
  }
}

void other_if(Box* b) {
  if (b == nullptr) {
    return;
  }
  if (b->noise > 0) {
    b->noise = 0;
  }
}

void route(Box* b, int k) {
  if (b == nullptr) {
    return;
  }
  switch (k) {
    case 1:
      set_x(b);
      break;
    case 2:
      clear_x(b);
      break;
    case 9:
      b->noise = 1;
      break;
  }
}

void post_later(Box* b) {
  auto fn = [b]() { later(b); };
  (void)fn;
}

}  // namespace effect_slice_fix
}  // namespace tuide
