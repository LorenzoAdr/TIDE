#pragma once

namespace tuide {

void enable_extended_key_reporting();
void disable_extended_key_reporting();

// Click + drag mouse only (DEC 1000/1002/1006). No DEC 1003 any-event motion.
void enable_click_drag_mouse_reporting();
void disable_click_drag_mouse_reporting();

}  // namespace tuide
