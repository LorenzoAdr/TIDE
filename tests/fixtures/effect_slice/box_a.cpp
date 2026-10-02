namespace tuide {
namespace effect_slice_fix {

void set_flag(int* p) {
  if (p == nullptr) {
    return;
  }
  *p = 1;
}

void clear_flag(int* p) {
  if (p == nullptr) {
    return;
  }
  *p = 0;
}

}  // namespace effect_slice_fix
}  // namespace tuide
