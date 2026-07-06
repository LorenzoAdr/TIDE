#pragma once

#include <cstdint>

namespace tgdb {

// Debounce for sending didChange to clangd and for showing diagnostics in the UI.
constexpr int64_t kLspDocumentDebounceMs = 1000;

}  // namespace tgdb
