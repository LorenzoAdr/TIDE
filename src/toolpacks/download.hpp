#pragma once

#include <cstdint>
#include <string>

#include "toolpacks/progress.hpp"

namespace tuide::toolpacks {

// Download url to dest_path using curl or wget. Returns empty on success.
// on_progress receives 0..100 while the transfer runs (size-based when
// expected_size > 0, otherwise best-effort from curl/wget progress output).
std::string download_url(const std::string& url, const std::string& dest_path,
                         ProgressFn on_progress = {}, std::uint64_t expected_size = 0);

// Hex lowercase sha256 of file; empty on failure.
std::string file_sha256(const std::string& path);

// Extract .tar.zst (or .tar) into dest_dir (created). Returns empty on success.
std::string extract_archive(const std::string& archive_path, const std::string& dest_dir);

// Host tools required to download and extract catalog toolpacks.
std::string toolpack_host_deps_error();

}  // namespace tuide::toolpacks
