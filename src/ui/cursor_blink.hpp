#pragma once

namespace tgdb::cursor_blink {

void tick();
bool visible();
void show();

inline int effective_col(int col) { return visible() ? col : -1; }

}  // namespace tgdb::cursor_blink