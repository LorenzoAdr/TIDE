#include "ai/tool_registry.hpp"

namespace tuide {

void ToolRegistry::register_tool(std::string name, std::string help, AiToolHandler handler) {
  tools_[std::move(name)] = Entry{std::move(help), std::move(handler)};
}

bool ToolRegistry::has(const std::string& name) const {
  return tools_.find(name) != tools_.end();
}

AiToolResult ToolRegistry::invoke(const std::string& name, const std::string& arg) const {
  const auto it = tools_.find(name);
  if (it == tools_.end() || !it->second.handler) {
    return AiToolResult{false, "unknown tool: " + name};
  }
  return it->second.handler(arg);
}

std::vector<std::pair<std::string, std::string>> ToolRegistry::list_tools() const {
  std::vector<std::pair<std::string, std::string>> out;
  out.reserve(tools_.size());
  for (const auto& [name, entry] : tools_) {
    out.emplace_back(name, entry.help);
  }
  return out;
}

void ToolRegistry::register_builtin_read_tools(ToolRegistry*, AiToolContext) {}

}  // namespace tuide
