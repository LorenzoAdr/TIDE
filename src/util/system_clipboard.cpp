#include "util/system_clipboard.hpp"

#include "util/shell_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace tuide {

namespace {

enum class ClipboardBackend { kNone, kWayland, kXClip, kXSel, kWsl };

// Minimal X11 types — resolved at runtime via dlopen("libX11.so.6"). No link-time X11 dep.
using X11Display = struct X11Display_;
using X11Window = unsigned long;
using X11Atom = unsigned long;
using X11Time = unsigned long;
using X11Bool = int;
using X11Status = int;
using X11Event = struct {
  int type;
  long pad[24];
};

struct X11SelectionEvent {
  int type;
  unsigned long serial;
  X11Bool send_event;
  X11Display* display;
  X11Window requestor;
  X11Atom selection;
  X11Atom target;
  X11Atom property;
  X11Time time;
};

struct X11SelectionRequestEvent {
  int type;
  unsigned long serial;
  X11Bool send_event;
  X11Display* display;
  X11Window owner;
  X11Window requestor;
  X11Atom selection;
  X11Atom target;
  X11Atom property;
  X11Time time;
};

constexpr int kX11SelectionClear = 29;
constexpr int kX11SelectionRequest = 30;
constexpr int kX11SelectionNotify = 31;
constexpr int kX11PropModeReplace = 0;
constexpr long kX11CurrentTime = 0;
constexpr unsigned long kX11None = 0;
constexpr unsigned long kX11AnyPropertyType = 0;
constexpr unsigned long kX11XaAtom = 4;
constexpr unsigned long kX11XaString = 31;
constexpr long kX11MaxPropertyBytes = 1024L * 1024L;
constexpr int kX11GetTimeoutMs = 150;

struct X11Api {
  void* handle = nullptr;
  int (*XInitThreads)() = nullptr;
  X11Display* (*XOpenDisplay)(const char*) = nullptr;
  int (*XCloseDisplay)(X11Display*) = nullptr;
  int (*XDefaultScreen)(X11Display*) = nullptr;
  X11Window (*XRootWindow)(X11Display*, int) = nullptr;
  X11Window (*XCreateSimpleWindow)(X11Display*, X11Window, int, int, unsigned int, unsigned int,
                                   unsigned int, unsigned long, unsigned long) = nullptr;
  int (*XDestroyWindow)(X11Display*, X11Window) = nullptr;
  X11Atom (*XInternAtom)(X11Display*, const char*, X11Bool) = nullptr;
  int (*XConvertSelection)(X11Display*, X11Atom, X11Atom, X11Atom, X11Window, X11Time) = nullptr;
  int (*XPending)(X11Display*) = nullptr;
  int (*XNextEvent)(X11Display*, X11Event*) = nullptr;
  int (*XGetWindowProperty)(X11Display*, X11Window, X11Atom, long, long, X11Bool, X11Atom,
                            X11Atom*, int*, unsigned long*, unsigned long*, unsigned char**) = nullptr;
  int (*XDeleteProperty)(X11Display*, X11Window, X11Atom) = nullptr;
  int (*XFree)(void*) = nullptr;
  int (*XFlush)(X11Display*) = nullptr;
  int (*XSync)(X11Display*, X11Bool) = nullptr;
  int (*XConnectionNumber)(X11Display*) = nullptr;
  int (*XSetSelectionOwner)(X11Display*, X11Atom, X11Window, X11Time) = nullptr;
  X11Window (*XGetSelectionOwner)(X11Display*, X11Atom) = nullptr;
  int (*XChangeProperty)(X11Display*, X11Window, X11Atom, X11Atom, int, int, const unsigned char*,
                         int) = nullptr;
  X11Status (*XSendEvent)(X11Display*, X11Window, X11Bool, long, X11Event*) = nullptr;
  int (*XSetErrorHandler)(int (*)(X11Display*, void*)) = nullptr;
};

X11Api g_x11;
std::once_flag g_x11_once;
std::atomic<bool> g_x11_ok{false};

std::mutex g_cli_mu;
bool g_cli_detected = false;
ClipboardBackend g_copy_backend = ClipboardBackend::kNone;
ClipboardBackend g_paste_backend = ClipboardBackend::kNone;

std::mutex g_owner_mu;
std::string g_owner_text;
std::atomic<bool> g_owner_stop{false};
X11Display* g_owner_dpy = nullptr;
X11Window g_owner_win = kX11None;
X11Atom g_atom_clipboard = kX11None;
X11Atom g_atom_utf8 = kX11None;
X11Atom g_atom_targets = kX11None;
X11Atom g_atom_text = kX11None;

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

template <typename T>
bool load_sym(void* handle, T& out, const char* name) {
  dlerror();
  void* sym = dlsym(handle, name);
  if (sym == nullptr) {
    return false;
  }
  out = reinterpret_cast<T>(sym);
  return true;
}

int x11_ignore_errors(X11Display*, void*) {
  return 0;
}

bool load_x11_api() {
  if (g_x11.handle != nullptr) {
    return g_x11_ok.load();
  }
  void* handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
  if (handle == nullptr) {
    handle = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
  }
  if (handle == nullptr) {
    return false;
  }
  g_x11.handle = handle;
  const bool ok =
      load_sym(handle, g_x11.XInitThreads, "XInitThreads") &&
      load_sym(handle, g_x11.XOpenDisplay, "XOpenDisplay") &&
      load_sym(handle, g_x11.XCloseDisplay, "XCloseDisplay") &&
      load_sym(handle, g_x11.XDefaultScreen, "XDefaultScreen") &&
      load_sym(handle, g_x11.XRootWindow, "XRootWindow") &&
      load_sym(handle, g_x11.XCreateSimpleWindow, "XCreateSimpleWindow") &&
      load_sym(handle, g_x11.XDestroyWindow, "XDestroyWindow") &&
      load_sym(handle, g_x11.XInternAtom, "XInternAtom") &&
      load_sym(handle, g_x11.XConvertSelection, "XConvertSelection") &&
      load_sym(handle, g_x11.XPending, "XPending") &&
      load_sym(handle, g_x11.XNextEvent, "XNextEvent") &&
      load_sym(handle, g_x11.XGetWindowProperty, "XGetWindowProperty") &&
      load_sym(handle, g_x11.XDeleteProperty, "XDeleteProperty") &&
      load_sym(handle, g_x11.XFree, "XFree") &&
      load_sym(handle, g_x11.XFlush, "XFlush") &&
      load_sym(handle, g_x11.XSync, "XSync") &&
      load_sym(handle, g_x11.XConnectionNumber, "XConnectionNumber") &&
      load_sym(handle, g_x11.XSetSelectionOwner, "XSetSelectionOwner") &&
      load_sym(handle, g_x11.XGetSelectionOwner, "XGetSelectionOwner") &&
      load_sym(handle, g_x11.XChangeProperty, "XChangeProperty") &&
      load_sym(handle, g_x11.XSendEvent, "XSendEvent") &&
      load_sym(handle, g_x11.XSetErrorHandler, "XSetErrorHandler");
  if (!ok) {
    dlclose(handle);
    g_x11 = {};
    return false;
  }
  g_x11.XInitThreads();
  g_x11.XSetErrorHandler(x11_ignore_errors);
  g_x11_ok.store(true);
  return true;
}

bool display_env_present() {
  const char* display = std::getenv("DISPLAY");
  return display != nullptr && *display != '\0';
}

bool ensure_x11() {
  std::call_once(g_x11_once, [] {
    if (!display_env_present()) {
      return;
    }
    load_x11_api();
  });
  return g_x11_ok.load();
}

bool wait_x11_event(X11Display* dpy, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (g_x11.XPending(dpy) > 0) {
      return true;
    }
    pollfd pfd{};
    pfd.fd = g_x11.XConnectionNumber(dpy);
    pfd.events = POLLIN;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now())
                          .count();
    if (left <= 0) {
      break;
    }
    poll(&pfd, 1, static_cast<int>(std::min<long>(left, 20)));
  }
  return g_x11.XPending(dpy) > 0;
}

std::string read_x11_property(X11Display* dpy, X11Window win, X11Atom prop) {
  X11Atom actual_type = kX11None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char* data = nullptr;
  const int status =
      g_x11.XGetWindowProperty(dpy, win, prop, 0, kX11MaxPropertyBytes / 4, 0 /* False */,
                               kX11AnyPropertyType, &actual_type, &actual_format, &nitems,
                               &bytes_after, &data);
  if (status != 0 /* Success */ || actual_format != 8) {
    if (data != nullptr) {
      g_x11.XFree(data);
    }
    return {};
  }
  std::string out;
  if (data != nullptr && nitems > 0) {
    out.assign(reinterpret_cast<const char*>(data), static_cast<std::size_t>(nitems));
  }
  if (data != nullptr) {
    g_x11.XFree(data);
  }
  g_x11.XDeleteProperty(dpy, win, prop);
  while (!out.empty() && out.back() == '\0') {
    out.pop_back();
  }
  return out;
}

// Returns false if the X11 round-trip failed (timeout / no notify). empty ok=true means
// a successful read of an empty clipboard.
bool x11_get_clipboard_once(X11Atom target, std::string* out) {
  out->clear();
  X11Display* dpy = g_x11.XOpenDisplay(nullptr);
  if (dpy == nullptr) {
    return false;
  }
  const int screen = g_x11.XDefaultScreen(dpy);
  const X11Window root = g_x11.XRootWindow(dpy, screen);
  const X11Window win = g_x11.XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  const X11Atom clipboard = g_x11.XInternAtom(dpy, "CLIPBOARD", 0);
  const X11Atom prop = g_x11.XInternAtom(dpy, "TUIDE_CLIPBOARD", 0);

  g_x11.XConvertSelection(dpy, clipboard, target, prop, win, kX11CurrentTime);
  g_x11.XFlush(dpy);

  bool got_notify = false;
  bool conversion_ok = false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kX11GetTimeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now())
                          .count();
    if (!wait_x11_event(dpy, static_cast<int>(std::max<long>(left, 1)))) {
      break;
    }
    X11Event event{};
    g_x11.XNextEvent(dpy, &event);
    if (event.type != kX11SelectionNotify) {
      continue;
    }
    got_notify = true;
    auto* sel = reinterpret_cast<X11SelectionEvent*>(&event);
    if (sel->property != kX11None) {
      *out = read_x11_property(dpy, win, sel->property);
      conversion_ok = true;
    }
    break;
  }

  g_x11.XDestroyWindow(dpy, win);
  g_x11.XCloseDisplay(dpy);
  return got_notify && conversion_ok;
}

bool x11_get_clipboard(std::string* out) {
  out->clear();
  if (!ensure_x11()) {
    return false;
  }
  // Prefer UTF8_STRING; fall back to STRING (Latin-1 / legacy).
  X11Display* dpy = g_x11.XOpenDisplay(nullptr);
  if (dpy == nullptr) {
    g_x11_ok.store(false);
    return false;
  }
  const X11Atom utf8 = g_x11.XInternAtom(dpy, "UTF8_STRING", 0);
  g_x11.XCloseDisplay(dpy);

  if (x11_get_clipboard_once(utf8, out)) {
    return true;
  }
  return x11_get_clipboard_once(static_cast<X11Atom>(kX11XaString), out);
}

void serve_selection_request(const X11SelectionRequestEvent& req, const std::string& text) {
  X11Event reply{};
  auto* notify = reinterpret_cast<X11SelectionEvent*>(&reply);
  notify->type = kX11SelectionNotify;
  notify->display = req.display;
  notify->requestor = req.requestor;
  notify->selection = req.selection;
  notify->target = req.target;
  notify->property = req.property;
  notify->time = req.time;

  if (req.property == kX11None) {
    notify->property = kX11None;
    g_x11.XSendEvent(req.display, req.requestor, 0, 0, &reply);
    g_x11.XFlush(req.display);
    return;
  }

  if (req.target == g_atom_targets) {
    const X11Atom targets[] = {g_atom_targets, g_atom_utf8, static_cast<X11Atom>(kX11XaString),
                               g_atom_text};
    g_x11.XChangeProperty(req.display, req.requestor, req.property, kX11XaAtom, 32,
                          kX11PropModeReplace,
                          reinterpret_cast<const unsigned char*>(targets),
                          static_cast<int>(sizeof(targets) / sizeof(targets[0])));
  } else if (req.target == g_atom_utf8 || req.target == static_cast<X11Atom>(kX11XaString) ||
             req.target == g_atom_text) {
    const X11Atom type =
        (req.target == static_cast<X11Atom>(kX11XaString)) ? kX11XaString : g_atom_utf8;
    g_x11.XChangeProperty(req.display, req.requestor, req.property, type, 8, kX11PropModeReplace,
                          reinterpret_cast<const unsigned char*>(text.data()),
                          static_cast<int>(text.size()));
  } else {
    notify->property = kX11None;
  }

  g_x11.XSendEvent(req.display, req.requestor, 0, 0, &reply);
  g_x11.XFlush(req.display);
}

void owner_thread_main() {
  while (!g_owner_stop.load()) {
    X11Display* dpy = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_owner_mu);
      dpy = g_owner_dpy;
    }
    if (dpy == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    if (!wait_x11_event(dpy, 50)) {
      continue;
    }
    X11Event event{};
    g_x11.XNextEvent(dpy, &event);
    if (event.type == kX11SelectionRequest) {
      std::string text;
      {
        std::lock_guard<std::mutex> lock(g_owner_mu);
        text = g_owner_text;
      }
      serve_selection_request(*reinterpret_cast<X11SelectionRequestEvent*>(&event), text);
    } else if (event.type == kX11SelectionClear) {
      // Another client took CLIPBOARD; keep the thread for the next set.
    }
  }
}

bool x11_set_clipboard(const std::string& text) {
  if (!ensure_x11()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_owner_mu);
  g_owner_text = text;

  if (g_owner_dpy == nullptr) {
    g_owner_dpy = g_x11.XOpenDisplay(nullptr);
    if (g_owner_dpy == nullptr) {
      g_x11_ok.store(false);
      return false;
    }
    const int screen = g_x11.XDefaultScreen(g_owner_dpy);
    const X11Window root = g_x11.XRootWindow(g_owner_dpy, screen);
    g_owner_win = g_x11.XCreateSimpleWindow(g_owner_dpy, root, 0, 0, 1, 1, 0, 0, 0);
    g_atom_clipboard = g_x11.XInternAtom(g_owner_dpy, "CLIPBOARD", 0);
    g_atom_utf8 = g_x11.XInternAtom(g_owner_dpy, "UTF8_STRING", 0);
    g_atom_targets = g_x11.XInternAtom(g_owner_dpy, "TARGETS", 0);
    g_atom_text = g_x11.XInternAtom(g_owner_dpy, "TEXT", 0);
    g_owner_stop.store(false);
    // Detached: serves SelectionRequest until process exit. Avoid joining from
    // atexit (Xlib + teardown races).
    std::thread(owner_thread_main).detach();
  }

  g_x11.XSetSelectionOwner(g_owner_dpy, g_atom_clipboard, g_owner_win, kX11CurrentTime);
  g_x11.XFlush(g_owner_dpy);
  return g_x11.XGetSelectionOwner(g_owner_dpy, g_atom_clipboard) == g_owner_win;
}

ClipboardBackend detect_copy_backend_uncached() {
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

ClipboardBackend detect_paste_backend_uncached() {
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

void ensure_cli_backends() {
  std::lock_guard<std::mutex> lock(g_cli_mu);
  if (g_cli_detected) {
    return;
  }
  g_copy_backend = detect_copy_backend_uncached();
  g_paste_backend = detect_paste_backend_uncached();
  g_cli_detected = true;
}

bool set_cli_clipboard(const std::string& text) {
  ensure_cli_backends();
  switch (g_copy_backend) {
    case ClipboardBackend::kWsl:
      return run_shell_stdin("clip.exe", text, 2);
    case ClipboardBackend::kWayland:
      return run_shell_stdin("wl-copy", text, 2);
    case ClipboardBackend::kXClip:
      return run_shell_stdin("xclip -selection clipboard", text, 2);
    case ClipboardBackend::kXSel:
      return run_shell_stdin("xsel --clipboard --input", text, 2);
    case ClipboardBackend::kNone:
      break;
  }
  return false;
}

std::string get_cli_clipboard() {
  ensure_cli_backends();
  switch (g_paste_backend) {
    case ClipboardBackend::kWayland:
      return run_shell_capture("wl-paste -n", 2);
    case ClipboardBackend::kXClip:
      return run_shell_capture("xclip -o -selection clipboard", 2);
    case ClipboardBackend::kXSel:
      return run_shell_capture("xsel --clipboard --output", 2);
    case ClipboardBackend::kWsl: {
      const std::string out =
          run_shell_capture("powershell.exe -NoProfile -Command Get-Clipboard", 3);
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

}  // namespace

void warm_system_clipboard() {
  ensure_x11();
  ensure_cli_backends();
}

bool set_system_clipboard(const std::string& text) {
  bool ok = false;
  if (ensure_x11()) {
    ok = x11_set_clipboard(text);
  }
  if (!ok) {
    ok = set_cli_clipboard(text);
  }
  copy_via_osc52(text);
  return ok;
}

std::string get_system_clipboard() {
  std::string text;
  if (x11_get_clipboard(&text)) {
    return text;
  }
  return get_cli_clipboard();
}

}  // namespace tuide
