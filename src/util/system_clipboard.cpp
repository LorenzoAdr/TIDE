#include "util/system_clipboard.hpp"

#include "util/shell_utils.hpp"

#include <cstdlib>
#include <iostream>

namespace tuide {

namespace {

enum class ClipboardBackend { kNone, kWayland, kXClip, kXSel, kWsl };

std::string base64_encode(const std::string& input) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  std::size_t i = 0;
  while (i + 2 < input.size()) {
    const unsigned v = (static_cast<unsigned char>(input[i]) << 16) |
                       (static_cast<unsigned char>(input[i + 1]) << 8) |
                       static_cast<unsigned char>(input[i + 2]);
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back(kTable[(v >> 6) & 0x3F]);
    out.push_back(kTable[v & 0x3F]);
    i += 3;
  }
  if (i < input.size()) {
    const unsigned v = static_cast<unsigned char>(input[i]) << 16;
    out.push_back(kTable[(v >> 18) & 0x3F]);
    if (i + 1 < input.size()) {
      const unsigned v2 = v | (static_cast<unsigned char>(input[i + 1]) << 8);
      out.push_back(kTable[(v2 >> 12) & 0x3F]);
      out.push_back(kTable[(v2 >> 6) & 0x3F]);
      out.push_back('=');
    } else {
      out.push_back(kTable[(v >> 12) & 0x3F]);
      out.push_back('=');
      out.push_back('=');
    }
  }
  return out;
}

void copy_via_osc52(const std::string& text) {
  if (text.empty()) {
    return;
  }
  std::cout << "\033]52;c;" << base64_encode(text) << "\033\\";
  std::cout.flush();
}

ClipboardBackend detect_copy_backend() {
  const char* wsl = std::getenv("WSL_DISTRO_NAME");
  if (wsl != nullptr && *wsl != '\0' && command_exists("clip.exe")) {
    return ClipboardBackend::kWsl;
  }
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if (wayland != nullptr && *wayland != '\0' && command_exists("wl-copy")) {
    return ClipboardBackend::kWayland;
  }
  const char* display = std::getenv("DISPLAY");
  if (display != nullptr && *display != '\0') {
    if (command_exists("xclip")) {
      return ClipboardBackend::kXClip;
    }
    if (command_exists("xsel")) {
      return ClipboardBackend::kXSel;
    }
  }
  return ClipboardBackend::kNone;
}

ClipboardBackend detect_paste_backend() {
  const char* wsl = std::getenv("WSL_DISTRO_NAME");
  if (wsl != nullptr && *wsl != '\0') {
    if (command_exists("wl-paste")) {
      return ClipboardBackend::kWayland;
    }
    if (command_exists("powershell.exe")) {
      return ClipboardBackend::kWsl;
    }
  }
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if (wayland != nullptr && *wayland != '\0' && command_exists("wl-paste")) {
    return ClipboardBackend::kWayland;
  }
  const char* display = std::getenv("DISPLAY");
  if (display != nullptr && *display != '\0') {
    if (command_exists("xclip")) {
      return ClipboardBackend::kXClip;
    }
    if (command_exists("xsel")) {
      return ClipboardBackend::kXSel;
    }
  }
  return ClipboardBackend::kNone;
}

}  // namespace

bool set_system_clipboard(const std::string& text) {
  bool ok = false;
  switch (detect_copy_backend()) {
    case ClipboardBackend::kWsl:
      ok = run_shell_stdin("clip.exe", text, 2);
      break;
    case ClipboardBackend::kWayland:
      ok = run_shell_stdin("wl-copy", text, 2);
      break;
    case ClipboardBackend::kXClip:
      ok = run_shell_stdin("xclip -selection clipboard", text, 2);
      break;
    case ClipboardBackend::kXSel:
      ok = run_shell_stdin("xsel --clipboard --input", text, 2);
      break;
    case ClipboardBackend::kNone:
      break;
  }
  copy_via_osc52(text);
  return ok;
}

std::string get_system_clipboard() {
  switch (detect_paste_backend()) {
    case ClipboardBackend::kWayland:
      return run_shell_capture("wl-paste -n", 2);
    case ClipboardBackend::kXClip:
      return run_shell_capture("xclip -o -selection clipboard", 2);
    case ClipboardBackend::kXSel:
      return run_shell_capture("xsel --clipboard --output", 2);
    case ClipboardBackend::kWsl: {
      const std::string out = run_shell_capture(
          "powershell.exe -NoProfile -Command Get-Clipboard", 3);
      if (!out.empty() && out.back() == '\n') {
        return out.substr(0, out.size() - 1);
      }
      return out;
    }
    case ClipboardBackend::kNone:
      break;
  }
  return {};
}

}  // namespace tuide
