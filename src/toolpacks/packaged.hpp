#pragma once

#include <string>

namespace tuide::toolpacks {

// True if path is not a clean core: legacy TUIDTPK1 trailer, *.AppImage, or AppDir.
bool is_packaged_binary(const std::string& path);

// Legacy trailer detection (read-only; used to block re-export of old builds).
bool binary_has_legacy_toolpack_trailer(const std::string& binary_path);

bool path_looks_like_appimage(const std::string& path);
bool path_looks_like_appdir(const std::string& path);

}  // namespace tuide::toolpacks
