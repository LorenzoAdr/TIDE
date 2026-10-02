namespace tuide {
namespace effect_slice_fix {

void set_flag(int* p);

void kick(int* p) {
  if (p == nullptr) {
    return;
  }
  set_flag(p);
}

}  // namespace effect_slice_fix
}  // namespace tuide
