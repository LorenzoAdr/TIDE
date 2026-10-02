#pragma once

#include <string>

namespace tuide {

// Eagerly dlopen libX11 (if DISPLAY is set) and cache CLI clipboard backends.
void warm_system_clipboard();

bool set_system_clipboard(const std::string& text);
std::string get_system_clipboard();

// Non-blocking clipboard helpers for the UI thread.
void set_system_clipboard_async(const std::string& text);
void refresh_system_clipboard_cache_async();
// Returns cached system clipboard (empty if not yet warmed). Never blocks.
std::string peek_system_clipboard_cache();
bool system_clipboard_cache_ready();

}  // namespace tuide
