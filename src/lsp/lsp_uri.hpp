#pragma once

#include <string>

namespace tuide {

std::string path_to_uri(const std::string& absolute_path);
std::string uri_to_path(const std::string& uri);
std::string language_id_for_path(const std::string& path);
std::string normalize_lsp_path(const std::string& path);

}  // namespace tuide
