#pragma once

namespace tuide {

void enable_extended_key_reporting();
void disable_extended_key_reporting();

// Click + drag mouse only (DEC 1000/1002/1006). No DEC 1003 any-event motion.
void enable_click_drag_mouse_reporting();
void disable_click_drag_mouse_reporting();

// Bracketed paste (DEC 2004): terminals wrap clipboard paste in ESC[200~ … ESC[201~
// so the editor can insert raw text instead of treating newlines as Enter.
void enable_bracketed_paste();
void disable_bracketed_paste();

}  // namespace tuide
