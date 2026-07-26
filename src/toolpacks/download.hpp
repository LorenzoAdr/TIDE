#pragma once

#include <string>

namespace tuide::toolpacks {

// Download url to dest_path using curl or wget. Returns empty on success.
std::string download_url(const std::string& url, const std::string& dest_path);

// Hex lowercase sha256 of file; empty on failure.
std::string file_sha256(const std::string& path);

// Extract .tar.zst (or .tar) into dest_dir (created). Returns empty on success.
std::string extract_archive(const std::string& archive_path, const std::string& dest_dir);

}  // namespace tuide::toolpacks
