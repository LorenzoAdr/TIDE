#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tgdb::i18n {

std::string tr(std::string_view key);
std::string tr_fmt(std::string_view key, const std::vector<std::string>& args);

}  // namespace tgdb::i18n
