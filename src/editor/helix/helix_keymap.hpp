#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/helix/helix_commands.hpp"
#include "editor/helix/helix_state.hpp"

namespace tgdb {

struct HelixKeyTrieNode {
  std::optional<HelixCommand> command;
  std::unordered_map<std::string, std::unique_ptr<HelixKeyTrieNode>> children;
};

struct HelixKeyLookupResult {
  enum class Kind {
    kNone,
    kPending,
    kMatched,
  } kind = Kind::kNone;
  HelixCommand command = HelixCommand::kNone;
  const HelixKeyTrieNode* node = nullptr;
};

const HelixKeyTrieNode& helix_keymap_root(HelixMode mode);

HelixKeyLookupResult helix_lookup_key(HelixMode mode, const std::vector<std::string>& prefix,
                                      const std::string& key);

std::vector<std::pair<std::string, std::string>> helix_hint_entries(HelixMode mode,
                                                                    const HelixKeyTrieNode* node);

std::string helix_command_label(HelixCommand command);

}  // namespace tgdb
