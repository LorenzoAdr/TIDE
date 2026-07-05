#pragma once

#include <string_view>
#include <unordered_map>

namespace tgdb::i18n {

using StringTable = std::unordered_map<std::string_view, std::string_view>;

const StringTable& spanish_strings();
const StringTable& english_strings();

}  // namespace tgdb::i18n
