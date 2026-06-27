#include "indexer/trie.hpp"

#include <algorithm>
#include <cctype>

namespace tgdb {

namespace {

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

}  // namespace

int Trie::get_or_create_child(int node, char c) {
  auto it = nodes_[node].children.find(c);
  if (it != nodes_[node].children.end()) {
    return it->second;
  }
  const int child = static_cast<int>(nodes_.size());
  nodes_.push_back(Node{});
  nodes_[node].children[c] = child;
  return child;
}

void Trie::insert(const std::string& token) {
  if (nodes_.empty()) {
    nodes_.push_back(Node{});
  }
  int node = 0;
  for (char c : to_lower(token)) {
    node = get_or_create_child(node, c);
  }
  nodes_[node].paths.push_back(token);
}

void Trie::insert_path_tokens(const std::string& path) {
  std::string current;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!current.empty()) {
        insert(current);
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) {
    insert(current);
  }
  insert(path);
}

void Trie::collect_paths(int node, std::vector<std::string>* out) const {
  for (const auto& p : nodes_[node].paths) {
    out->push_back(p);
  }
  for (const auto& [_, child] : nodes_[node].children) {
    collect_paths(child, out);
  }
}

std::vector<std::string> Trie::search_substring(const std::string& query) const {
  if (nodes_.empty() || query.empty()) {
    return {};
  }
  const std::string q = to_lower(query);
  int node = 0;
  for (char c : q) {
    auto it = nodes_[node].children.find(c);
    if (it == nodes_[node].children.end()) {
      return {};
    }
    node = it->second;
  }
  std::vector<std::string> out;
  collect_paths(node, &out);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace tgdb
