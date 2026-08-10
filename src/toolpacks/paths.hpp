#pragma once

#include <string>

namespace tuide::toolpacks {

// Writable root for manifests and versioned payloads (installs always go here).
// Override with TUIDE_TOOLPACKS_ROOT; else $XDG_DATA_HOME/tuide/toolpacks
// or ~/.local/share/tuide/toolpacks.
std::string toolpacks_root();

// Optional read-only toolpacks shipped inside an AppImage/AppDir.
// Set by AppRun via TUIDE_TOOLPACKS_BUNDLED; empty when unset.
std::string bundled_toolpacks_root();

std::string manifest_path();
std::string bundled_manifest_path();
std::string cache_root();
std::string downloads_dir();

// Default catalog URL (catalog-latest tag on the project releases).
std::string default_catalog_url();

// True when toolpacks_root() is usable for installs (exists or creatable + writable).
bool toolpacks_root_is_writable();

}  // namespace tuide::toolpacks
