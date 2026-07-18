#pragma once

namespace tuide::cursor_blink {

void tick();
bool visible();
void show();

inline int effective_col(int col) { return visible() ? col : -1; }

}  // namespace tuide::cursor_blink