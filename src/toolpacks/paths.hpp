#pragma once

#include <string>

namespace tuide::toolpacks {

// Root for manifests and versioned payloads.
// Override with TUIDE_TOOLPACKS_ROOT; else $XDG_DATA_HOME/tuide/toolpacks
// or ~/.local/share/tuide/toolpacks.
std::string toolpacks_root();

std::string manifest_path();
std::string cache_root();
std::string downloads_dir();

// Default catalog URL (catalog-latest tag on the project releases).
std::string default_catalog_url();

}  // namespace tuide::toolpacks
