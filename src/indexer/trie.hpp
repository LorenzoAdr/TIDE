#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace tgdb {

class Trie {
 public:
  void insert(const std::string& token);
  void insert_path_tokens(const std::string& path);
  std::vector<std::string> search_substring(const std::string& query) const;

 private:
  struct Node {
    std::unordered_map<char, int> children;
    std::vector<std::string> paths;
  };

  std::vector<Node> nodes_;

  int get_or_create_child(int node, char c);
  void collect_paths(int node, std::vector<std::string>* out) const;
};

}  // namespace tgdb
