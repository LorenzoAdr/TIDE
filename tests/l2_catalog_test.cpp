#include "ai/l2_catalog.hpp"
#include "ai/l2_wave.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool ok, const char* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

void write_file(const fs::path& p, const std::string& body) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << body;
}

void seed_mini(const fs::path& root) {
  write_file(root / "src/ui/keys.cpp", R"(#include "ai/job.hpp"

int pending_insert;
struct Event {
  static int Escape;
};

void handle_key() {
  int event = 0;
  if (event == Event::Escape) {
    pending_insert = 1;
    cancel_job();
  }
}
)");
  write_file(root / "src/ui/settings.cpp", R"(struct Event {
  static int Escape;
};

void close_modal() {
  int event = 0;
  if (event == Event::Escape) {
    return;
  }
}
)");
  write_file(root / "src/ai/json.cpp", R"(void ai_trace_escape() {}
void strip_json() { ai_trace_escape(); }
void scan_json() {
  int escape = 0;
  if (escape) {
    return;
  }
}
)");
  write_file(root / "src/ai/job.hpp", R"(#pragma once

void cancel_job();
)");
  write_file(root / "src/ai/job.cpp", R"(#include "ai/job.hpp"

int busy;

void cancel_job() {
  busy = 0;
}
)");
  write_file(root / "src/git/remote.cpp", R"(void fetch_remote() {
  int sock = 0;
  (void)sock;
}
)");
  std::ostringstream fat;
  fat << "#include \"ai/job.hpp\"\n\n";
  for (int i = 0; i < 90; ++i) {
    fat << "void helper_" << i << "() { int x = static_cast<int>(" << i << "); (void)x; }\n";
  }
  fat << "void handle_escape() { pending_insert = 1; cancel_job(); }\n";
  write_file(root / "src/ui/fat.cpp", fat.str());
}

std::string first_barrio_id(const std::string& plano) {
  std::istringstream in(plano);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("  ", 0) != 0) {
      continue;
    }
    if (line.find("top ") != std::string::npos) {
      continue;
    }
    std::string rest = line.substr(2);
    const auto sp = rest.find(' ');
    return sp == std::string::npos ? rest : rest.substr(0, sp);
  }
  return {};
}

bool has_barrio(const tuide::Catalog& c, const std::string& id) {
  return std::any_of(c.barrios.begin(), c.barrios.end(),
                     [&](const tuide::CatalogBarrio& b) { return b.id == id; });
}

bool has_stem(const tuide::Catalog& c, const std::string& id) {
  return std::any_of(c.stems.begin(), c.stems.end(),
                     [&](const tuide::CatalogStem& s) { return s.id == id; });
}

bool stem_habla(const tuide::Catalog& c, const std::string& from, const std::string& to) {
  for (const auto& s : c.stems) {
    if (s.id != from) {
      continue;
    }
    return std::find(s.habla.begin(), s.habla.end(), to) != s.habla.end();
  }
  return false;
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "tuide_l2_catalog_mini";
  std::error_code ec;
  fs::remove_all(root, ec);
  seed_mini(root);

  tuide::Catalog cat;
  std::string err;
  expect(tuide::catalog_ensure(root.string(), &cat, &err, true), "rebuild");
  expect(err.empty() || cat.cards.size() > 0, "rebuild sin error fatal");
  expect(!cat.cache_hit, "force no es cache");
  expect(has_barrio(cat, "ui") && has_barrio(cat, "ai") && has_barrio(cat, "git"),
         "barrios ui/ai/git");
  expect(has_stem(cat, "keys") && has_stem(cat, "job") && has_stem(cat, "remote"),
         "stems keys/job/remote");
  expect(stem_habla(cat, "keys", "job"), "include keys→job");
  expect(tuide::catalog_id_kind(cat, "ui") == "barrio", "id ui es barrio");
  expect(tuide::catalog_id_kind(cat, "keys") == "stem", "id keys es stem");

  bool saw_handle = false;
  bool saw_cancel = false;
  for (const auto& card : cat.cards) {
    if (card.symbol == "handle_key") {
      saw_handle = true;
      expect(card.barrio == "ui" && card.stem == "keys", "ficha handle_key");
      expect(std::find(card.preds.begin(), card.preds.end(), "Escape") != card.preds.end() ||
                 std::find(card.preds.begin(), card.preds.end(), "Event") != card.preds.end(),
             "handle_key pred Event/Escape");
    }
    if (card.symbol == "cancel_job") {
      saw_cancel = true;
      expect(card.barrio == "ai" && card.stem == "job", "ficha cancel_job");
    }
  }
  expect(saw_handle && saw_cancel, "fichas handle_key y cancel_job");

  bool saw_escape = false;
  int helpers = 0;
  for (const auto& card : cat.cards) {
    if (card.symbol == "handle_escape") {
      saw_escape = true;
      expect(card.stem == "fat", "handle_escape en fat");
      expect(std::none_of(card.calls.begin(), card.calls.end(),
                          [](const std::string& c) { return c.find("static_cast") != std::string::npos; }),
             "calls sin static_cast");
      expect(std::find(card.calls.begin(), card.calls.end(), "cancel_job") != card.calls.end(),
             "handle_escape llama cancel_job");
    }
    if (card.symbol.rfind("helper_", 0) == 0) {
      ++helpers;
    }
  }
  expect(saw_escape, "cap por ranking conserva handle_escape");
  expect(helpers < 90, "no guarda los 90 helpers");

  tuide::Catalog again;
  std::string err2;
  expect(tuide::catalog_ensure(root.string(), &again, &err2), "segunda ensure");
  expect(again.cache_hit, "segunda vez cache hit");
  expect(again.censo == cat.censo, "censo estable");

  tuide::catalog_apply_heat(&cat, "key");
  const std::string plano_key = tuide::catalog_render_plano(cat);
  expect(plano_key.find("  ui ") != std::string::npos &&
             plano_key.find("  git ") != std::string::npos,
         "calor no oculta barrio frío");
  expect(first_barrio_id(plano_key) == "ui", "key calienta ui");

  tuide::catalog_apply_heat(&cat, "cancel_job");
  const std::string plano_cancel = tuide::catalog_render_plano(cat);
  expect(first_barrio_id(plano_cancel) == "ai", "cancel_job calienta ai");

  tuide::catalog_apply_heat(&cat, "cuando pulso fuera quiero que revierta");
  expect(first_barrio_id(tuide::catalog_render_plano(cat)) != "ui",
         "frase difusa no calibra el plano");
  tuide::catalog_apply_heat(&cat, std::vector<std::string>{"event", "key"});
  expect(first_barrio_id(tuide::catalog_render_plano(cat)) == "ui",
         "zonas event/key calientan ui");

  tuide::catalog_apply_heat(&cat, "escape");
  const std::string plano_esc = tuide::catalog_render_plano(cat);
  expect(first_barrio_id(plano_esc) == "ui", "escape calienta pred, no ai_trace_escape");
  const std::string zoom_esc = tuide::catalog_render_zoom(cat, "ui");
  expect(zoom_esc.find("Escape:") != std::string::npos || zoom_esc.find("escape:") != std::string::npos ||
             zoom_esc.find("Event:") != std::string::npos || zoom_esc.find("event:") != std::string::npos,
         "zoom barrio capa de pred");
  expect(zoom_esc.find("keys") != std::string::npos && zoom_esc.find("settings") != std::string::npos,
         "capa nombra stems con el pred");
  const std::string search_esc = tuide::catalog_render_search(cat);
  expect(search_esc.find("ai_trace_escape") == std::string::npos ||
             search_esc.find("handle_key") != std::string::npos,
         "search escape no lo gana el json");

  const std::string zoom_keys = tuide::catalog_render_zoom(cat, "keys");
  expect(zoom_keys.find("handle_key") != std::string::npos, "zoom stem nombra símbolo");
  expect(zoom_keys.find('{') == std::string::npos &&
             zoom_keys.find("pending_insert =") == std::string::npos,
         "zoom stem sin cuerpo");

  const std::string zoom_ui = tuide::catalog_render_zoom(cat, "ui");
  expect(zoom_ui.find("keys") != std::string::npos || zoom_ui.find("fat") != std::string::npos,
         "zoom barrio lista stems");
  expect(zoom_ui.find("roles=") == std::string::npos &&
             zoom_ui.find("calls=") == std::string::npos,
         "zoom barrio no baja a fichas");

  expect(!tuide::wave_control_zoom_id_ok("M1"), "zoom id no M*");
  expect(!tuide::wave_control_zoom_id_ok("src/ui/keys"), "zoom id no path");
  expect(tuide::wave_control_zoom_id_ok("keys"), "zoom id stem");

  fs::remove_all(root, ec);
  if (failures > 0) {
    std::cerr << failures << " fallos\n";
    return 1;
  }
  std::cout << "l2_catalog_test ok\n";
  return 0;
}
