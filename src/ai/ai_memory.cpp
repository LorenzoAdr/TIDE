#include "ai/ai_memory.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string trim_copy(std::string s) {
  const auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!s.empty() && is_space(s.front())) {
    s.erase(s.begin());
  }
  while (!s.empty() && is_space(s.back())) {
    s.pop_back();
  }
  return s;
}

// Recorta en frontera de carácter UTF-8 para no partir un multibyte.
void utf8_clip(std::string* s, std::size_t max_bytes) {
  if (s == nullptr || s->size() <= max_bytes) {
    return;
  }
  std::size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>((*s)[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  s->resize(cut);
}

// Una línea de memoria es un bullet markdown; se guarda plano para que el
// usuario pueda editar el fichero a mano sin romper nada.
std::string to_line(const std::string& fact) {
  std::string flat = fact;
  for (char& c : flat) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  flat = trim_copy(std::move(flat));
  utf8_clip(&flat, static_cast<std::size_t>(kAiMemoryFactChars));
  return flat;
}

bool write_items(const fs::path& path, const std::vector<std::string>& items, std::string* err) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + path.string();
    }
    return false;
  }
  out << "# Memoria del agente\n\n"
         "Hechos que no quiero repetir en cada consulta. Los escribe `/remember`\n"
         "y se pueden editar a mano: una línea `- hecho` por entrada.\n\n";
  for (const auto& item : items) {
    out << "- " << item << "\n";
  }
  if (!out) {
    if (err) {
      *err = "escritura incompleta en " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace

std::string ai_memory_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".tuide" / "ai" / "memory.md").string();
}

std::vector<std::string> ai_memory_items(const std::string& workspace_root) {
  std::vector<std::string> out;
  const std::string path = ai_memory_path(workspace_root);
  if (path.empty()) {
    return out;
  }
  std::ifstream in(path);
  if (!in) {
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    line = trim_copy(std::move(line));
    // Solo los bullets son hechos; el resto es la cabecera explicativa.
    if (line.rfind("- ", 0) != 0) {
      continue;
    }
    const std::string fact = trim_copy(line.substr(2));
    if (!fact.empty()) {
      out.push_back(fact);
    }
  }
  return out;
}

std::string ai_memory_load(const std::string& workspace_root, int max_chars) {
  const auto items = ai_memory_items(workspace_root);
  if (items.empty()) {
    return {};
  }
  // Se montan del más reciente al más antiguo y se corta al llegar al tope, de
  // modo que lo que se pierde es la memoria vieja.
  std::vector<std::string> kept;
  std::size_t used = 0;
  const std::size_t cap =
      max_chars > 0 ? static_cast<std::size_t>(max_chars) : std::string::npos;
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    const std::size_t cost = it->size() + 3;  // "- " + "\n"
    if (cap != std::string::npos && used + cost > cap) {
      break;
    }
    used += cost;
    kept.push_back(*it);
  }
  if (kept.empty()) {
    return {};
  }
  std::reverse(kept.begin(), kept.end());
  std::ostringstream out;
  for (const auto& fact : kept) {
    out << "- " << fact << "\n";
  }
  return out.str();
}

bool ai_memory_add(const std::string& workspace_root, const std::string& fact, std::string* err) {
  const std::string line = to_line(fact);
  if (line.empty()) {
    if (err) {
      *err = "nada que recordar";
    }
    return false;
  }
  const std::string path = ai_memory_path(workspace_root);
  if (path.empty()) {
    if (err) {
      *err = "sin workspace";
    }
    return false;
  }
  auto items = ai_memory_items(workspace_root);
  if (std::find(items.begin(), items.end(), line) != items.end()) {
    if (err) {
      *err = "ya estaba en la memoria";
    }
    return false;
  }
  items.push_back(line);
  // Tope duro: la memoria es un recordatorio, no un registro histórico.
  while (static_cast<int>(items.size()) > kAiMemoryMaxItems) {
    items.erase(items.begin());
  }
  return write_items(path, items, err);
}

bool ai_memory_remove(const std::string& workspace_root, int index_1based, std::string* err) {
  const std::string path = ai_memory_path(workspace_root);
  if (path.empty()) {
    if (err) {
      *err = "sin workspace";
    }
    return false;
  }
  auto items = ai_memory_items(workspace_root);
  if (index_1based < 1 || index_1based > static_cast<int>(items.size())) {
    if (err) {
      *err = items.empty() ? "la memoria está vacía"
                           : ("fuera de rango: hay " + std::to_string(items.size()) + " hechos");
    }
    return false;
  }
  items.erase(items.begin() + (index_1based - 1));
  return write_items(path, items, err);
}

}  // namespace tuide
