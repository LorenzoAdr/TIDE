#include "ai/project_contract.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {
namespace {

// Muestreo de fuentes para medir indentación: suficiente para decidir 2 vs 4 vs
// tabs sin recorrer un repo entero.
constexpr int kIndentSampleFiles = 40;
constexpr int kIndentSampleLines = 400;

std::string read_file(const fs::path& path, std::size_t max_bytes = 256 * 1024) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::string body;
  body.resize(max_bytes);
  in.read(body.data(), static_cast<std::streamsize>(max_bytes));
  body.resize(static_cast<std::size_t>(in.gcount()));
  return body;
}

bool exists_file(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

bool exists_dir(const fs::path& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}

void push_unique(std::vector<std::string>* out, std::string value) {
  if (value.empty() || out == nullptr) {
    return;
  }
  if (std::find(out->begin(), out->end(), value) == out->end()) {
    out->push_back(std::move(value));
  }
}

std::string lang_for_ext(const std::string& ext) {
  static const std::map<std::string, std::string> kMap = {
      {".cpp", "C++"},   {".cc", "C++"},    {".cxx", "C++"},  {".hpp", "C++"},
      {".hh", "C++"},    {".c", "C"},       {".h", "C/C++"},  {".rs", "Rust"},
      {".go", "Go"},     {".py", "Python"}, {".ts", "TypeScript"},
      {".tsx", "TypeScript"}, {".js", "JavaScript"}, {".jsx", "JavaScript"},
      {".java", "Java"}, {".kt", "Kotlin"}, {".rb", "Ruby"},  {".lua", "Lua"},
      {".sh", "Shell"},  {".zig", "Zig"},   {".swift", "Swift"}, {".cs", "C#"},
      {".php", "PHP"},   {".f90", "Fortran"}, {".ex", "Elixir"},
  };
  const auto it = kMap.find(ext);
  return it == kMap.end() ? std::string() : it->second;
}

bool is_code_ext(const std::string& ext) {
  return !lang_for_ext(ext).empty();
}

// Directorios que nunca son fuente del proyecto. `.claude/worktrees` son copias
// completas del repo (una por rama en vuelo) y contaminan cualquier búsqueda.
const std::array<const char*, 12> kExcludeCandidates = {
    "third_party", "vendor",  "node_modules", "build",  "dist", "target",
    ".venv",       "venv",    ".tuide",       ".claude", "out",  "Pods"};

struct IndentStats {
  int two = 0;
  int four = 0;
  int tabs = 0;
};

void sample_indent(const fs::path& file, IndentStats* stats) {
  std::ifstream in(file);
  if (!in) {
    return;
  }
  std::string line;
  int seen = 0;
  while (seen < kIndentSampleLines && std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    ++seen;
    if (line[0] == '\t') {
      ++stats->tabs;
      continue;
    }
    std::size_t spaces = 0;
    while (spaces < line.size() && line[spaces] == ' ') {
      ++spaces;
    }
    if (spaces == 0 || spaces >= line.size()) {
      continue;
    }
    // Solo el primer nivel de indentación informa del ancho: 2 y 4 se
    // distinguen mal en niveles profundos (4 espacios puede ser 2+2).
    if (spaces == 2) {
      ++stats->two;
    } else if (spaces == 4) {
      ++stats->four;
    }
  }
}

std::vector<std::string> json_scripts(const fs::path& package_json) {
  std::vector<std::string> out;
  const std::string body = read_file(package_json);
  if (body.empty()) {
    return out;
  }
  try {
    const auto j = nlohmann::json::parse(body);
    if (j.contains("scripts") && j["scripts"].is_object()) {
      for (const auto& item : j["scripts"].items()) {
        out.push_back(item.key());
      }
    }
  } catch (...) {
  }
  return out;
}

bool file_mentions(const fs::path& path, const std::vector<std::string>& needles) {
  const std::string body = read_file(path);
  for (const auto& n : needles) {
    if (body.find(n) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Tareas que el usuario ya declaró en `.tuide/config.json`: si existen, son la
// verdad del proyecto y van antes que cualquier comando deducido.
void probe_tuide_tasks(const fs::path& root, ProjectProbe* probe) {
  const std::string body = read_file(root / ".tuide" / "config.json");
  if (body.empty()) {
    return;
  }
  try {
    const auto j = nlohmann::json::parse(body);
    if (!j.contains("ai") || !j["ai"].is_object()) {
      return;
    }
    const auto& ai = j["ai"];
    if (!ai.contains("tasks") || !ai["tasks"].is_object()) {
      return;
    }
    for (const auto& item : ai["tasks"].items()) {
      std::string cmd;
      if (item.value().is_string()) {
        cmd = item.value().get<std::string>();
      } else if (item.value().is_object() && item.value().contains("command") &&
                 item.value()["command"].is_string()) {
        cmd = item.value()["command"].get<std::string>();
      }
      if (cmd.empty()) {
        continue;
      }
      const std::string name = item.key();
      if (name.find("test") != std::string::npos) {
        push_unique(&probe->test_cmds, cmd);
      } else if (name.find("launch") != std::string::npos ||
                 name.find("run") != std::string::npos) {
        push_unique(&probe->run_cmds, cmd);
      } else {
        push_unique(&probe->build_cmds, cmd);
      }
    }
  } catch (...) {
  }
}

}  // namespace

ProjectProbe project_probe(const std::string& workspace_root) {
  ProjectProbe probe;
  if (workspace_root.empty()) {
    return probe;
  }
  const fs::path root(workspace_root);
  probe.name = root.filename().string();
  if (probe.name.empty()) {
    probe.name = root.string();
  }

  // 1. Un script envoltorio en la raíz es la respuesta correcta del proyecto, y
  //    manda sobre el comando genérico del sistema de build.
  for (const char* script : {"compile.sh", "build.sh", "make.sh", "bootstrap.sh"}) {
    if (exists_file(root / script)) {
      push_unique(&probe.build_cmds, std::string("./") + script);
      break;
    }
  }
  for (const char* script : {"launch.sh", "run.sh", "start.sh"}) {
    if (exists_file(root / script)) {
      push_unique(&probe.run_cmds, std::string("./") + script);
      break;
    }
  }
  for (const char* script : {"test.sh", "tests.sh", "check.sh"}) {
    if (exists_file(root / script)) {
      push_unique(&probe.test_cmds, std::string("./") + script);
      break;
    }
  }
  probe_tuide_tasks(root, &probe);

  // 2. Sistema de build por marcadores.
  if (exists_file(root / "CMakeLists.txt")) {
    push_unique(&probe.kinds, "cmake");
    push_unique(&probe.build_cmds, "cmake -S . -B build && cmake --build build -j");
    const fs::path cml = root / "CMakeLists.txt";
    if (file_mentions(cml, {"enable_testing", "add_test(", "include(CTest"})) {
      push_unique(&probe.test_cmds, "ctest --test-dir build --output-on-failure");
    } else if (file_mentions(cml, {"_test"})) {
      // Binarios de test sueltos: el patrón de este repo. No hay ctest que
      // llamar, así que se nombra el patrón en vez de inventar un comando.
      push_unique(&probe.test_cmds,
                  "make -C build -j4 <nombre>_test && ./build/<nombre>_test");
    }
  }
  if (exists_file(root / "Makefile") || exists_file(root / "makefile")) {
    push_unique(&probe.kinds, "make");
    push_unique(&probe.build_cmds, "make");
    const fs::path mk = exists_file(root / "Makefile") ? root / "Makefile" : root / "makefile";
    if (file_mentions(mk, {"\ntest:", "\ncheck:"})) {
      push_unique(&probe.test_cmds, "make test");
    }
  }
  if (exists_file(root / "package.json")) {
    push_unique(&probe.kinds, "npm");
    std::string runner = "npm run";
    if (exists_file(root / "pnpm-lock.yaml")) {
      runner = "pnpm";
    } else if (exists_file(root / "yarn.lock")) {
      runner = "yarn";
    }
    const auto scripts = json_scripts(root / "package.json");
    auto has = [&scripts](const char* name) {
      return std::find(scripts.begin(), scripts.end(), name) != scripts.end();
    };
    if (has("build")) {
      push_unique(&probe.build_cmds, runner + " build");
    }
    if (has("test")) {
      push_unique(&probe.test_cmds, runner + " test");
    }
    if (has("dev")) {
      push_unique(&probe.run_cmds, runner + " dev");
    } else if (has("start")) {
      push_unique(&probe.run_cmds, runner + " start");
    }
  }
  if (exists_file(root / "Cargo.toml")) {
    push_unique(&probe.kinds, "cargo");
    push_unique(&probe.build_cmds, "cargo build");
    push_unique(&probe.test_cmds, "cargo test");
    push_unique(&probe.run_cmds, "cargo run");
  }
  if (exists_file(root / "go.mod")) {
    push_unique(&probe.kinds, "go");
    push_unique(&probe.build_cmds, "go build ./...");
    push_unique(&probe.test_cmds, "go test ./...");
  }
  if (exists_file(root / "pyproject.toml") || exists_file(root / "setup.py") ||
      exists_file(root / "requirements.txt")) {
    push_unique(&probe.kinds, "python");
    if (exists_dir(root / "tests") || exists_file(root / "pytest.ini") ||
        (exists_file(root / "pyproject.toml") &&
         file_mentions(root / "pyproject.toml", {"pytest"}))) {
      push_unique(&probe.test_cmds, "pytest");
    }
  }
  if (exists_file(root / "meson.build")) {
    push_unique(&probe.kinds, "meson");
    push_unique(&probe.build_cmds, "meson setup build && meson compile -C build");
    push_unique(&probe.test_cmds, "meson test -C build");
  }
  if (exists_file(root / "pom.xml")) {
    push_unique(&probe.kinds, "maven");
    push_unique(&probe.build_cmds, "mvn package");
    push_unique(&probe.test_cmds, "mvn test");
  }
  if (exists_file(root / "gradlew")) {
    push_unique(&probe.kinds, "gradle");
    push_unique(&probe.build_cmds, "./gradlew build");
    push_unique(&probe.test_cmds, "./gradlew test");
  }

  // 3. Directorios excluidos presentes.
  for (const char* dir : kExcludeCandidates) {
    if (exists_dir(root / dir)) {
      push_unique(&probe.excluded_dirs, dir);
    }
  }

  // 4. Lenguajes dominantes + indentación, sobre las fuentes reales.
  std::map<std::string, int> lang_counts;
  IndentStats indent;
  int sampled = 0;
  {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
      if (ec) {
        break;
      }
      const fs::path& p = it->path();
      std::error_code dec;
      if (fs::is_directory(p, dec)) {
        const std::string name = p.filename().string();
        const bool excluded =
            std::find(probe.excluded_dirs.begin(), probe.excluded_dirs.end(), name) !=
            probe.excluded_dirs.end();
        if (excluded || (name.size() > 1 && name[0] == '.')) {
          it.disable_recursion_pending();
        }
        continue;
      }
      const std::string ext = p.extension().string();
      if (!is_code_ext(ext)) {
        continue;
      }
      lang_counts[lang_for_ext(ext)]++;
      // La indentación se mide solo en el lenguaje dominante del repo, que a
      // estas alturas aún no se conoce: se muestrean los primeros ficheros de
      // código y basta, porque un repo mezcla pero no al 50 %.
      if (sampled < kIndentSampleFiles && ext != ".md") {
        sample_indent(p, &indent);
        ++sampled;
      }
    }
  }
  {
    std::vector<std::pair<std::string, int>> ordered(lang_counts.begin(), lang_counts.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < ordered.size() && i < 3; ++i) {
      probe.languages.push_back(ordered[i].first);
    }
  }
  if (indent.tabs > indent.two && indent.tabs > indent.four) {
    probe.indent_tabs = true;
  } else if (indent.four > indent.two) {
    probe.indent_width = 4;
  } else if (indent.two > 0) {
    probe.indent_width = 2;
  }

  // 5. ¿El fichero de estilo dice algo distinto de lo medido? Un .clang-format
  //    desfasado es peor que ninguno: el agente lo obedece y rompe el estilo real.
  if (exists_file(root / ".clang-format")) {
    probe.style_file = ".clang-format";
    const std::string body = read_file(root / ".clang-format");
    const bool says_tabs = body.find("UseTab: Always") != std::string::npos ||
                           body.find("UseTab: ForIndentation") != std::string::npos;
    const bool says_four = body.find("IndentWidth: 4") != std::string::npos;
    const bool says_two = body.find("IndentWidth: 2") != std::string::npos;
    if (probe.indent_tabs) {
      probe.style_file_disagrees = !says_tabs;
    } else if (probe.indent_width == 2) {
      probe.style_file_disagrees = says_four || says_tabs;
    } else if (probe.indent_width == 4) {
      probe.style_file_disagrees = says_two || says_tabs;
    }
  }
  return probe;
}

std::string project_contract_template(const ProjectProbe& probe) {
  std::ostringstream out;
  out << "# Contrato del proyecto — " << (probe.name.empty() ? "este repo" : probe.name) << "\n\n";
  out << "Lo lee el agente de IA de tuide en CADA consulta: breve y cierto. Si algo\n"
         "deja de ser verdad, corrígelo aquí antes de que el agente lo repita.\n"
         "Generado por `/init`; las líneas TODO son las que solo sabes tú.\n\n";

  if (!probe.languages.empty() || !probe.kinds.empty()) {
    out << "Proyecto";
    if (!probe.languages.empty()) {
      out << " en " << probe.languages[0];
      for (std::size_t i = 1; i < probe.languages.size(); ++i) {
        out << " + " << probe.languages[i];
      }
    }
    if (!probe.kinds.empty()) {
      out << " (" << probe.kinds[0];
      for (std::size_t i = 1; i < probe.kinds.size(); ++i) {
        out << ", " << probe.kinds[i];
      }
      out << ")";
    }
    out << ".\n\n";
  }

  out << "## Compilar y ejecutar\n\n```bash\n";
  if (probe.build_cmds.empty()) {
    out << "# TODO: comando de build (no se detectó ningún sistema conocido)\n";
  } else {
    for (const auto& c : probe.build_cmds) {
      out << c << "\n";
    }
  }
  for (const auto& c : probe.run_cmds) {
    out << c << "\n";
  }
  out << "```\n\n";

  out << "## Tests\n\n```bash\n";
  if (probe.test_cmds.empty()) {
    out << "# TODO: cómo se corren los tests (no se detectó runner)\n";
  } else {
    for (const auto& c : probe.test_cmds) {
      out << c << "\n";
    }
  }
  out << "```\n\n";
  out << "TODO: ¿desde qué directorio hay que correrlos? ¿Hay tests ya en rojo que\n"
         "no son del trabajo en curso? Decirlo aquí evita que el agente los \"arregle\".\n\n";

  out << "## Convenciones\n\n";
  if (probe.indent_tabs) {
    out << "- Indentación: **tabuladores** (medido en las fuentes).\n";
  } else if (probe.indent_width > 0) {
    out << "- Indentación: **" << probe.indent_width << " espacios** (medido en las fuentes).\n";
  } else {
    out << "- TODO: indentación y ancho de línea.\n";
  }
  if (probe.style_file_disagrees && !probe.style_file.empty()) {
    out << "- Ojo: `" << probe.style_file
        << "` declara algo distinto de lo que usan las fuentes. Manda el archivo\n"
           "  que estés editando, no el fichero de estilo.\n";
  }
  out << "- TODO: idioma de comentarios y de mensajes de commit.\n";
  out << "- TODO: qué NO hacer (dependencias nuevas, reescrituras, formateo masivo).\n\n";

  out << "## Zonas prohibidas\n\n";
  if (probe.excluded_dirs.empty()) {
    out << "TODO: directorios que el agente no debe editar ni buscar.\n\n";
  } else {
    out << "No editar ni buscar en:";
    for (std::size_t i = 0; i < probe.excluded_dirs.size(); ++i) {
      out << (i ? ", " : " ") << "`" << probe.excluded_dirs[i] << "/`";
    }
    out << ".\n\n";
  }

  out << "## Mapa del repo\n\n";
  out << "TODO: dos o tres líneas sobre dónde vive cada cosa y qué parte es legado.\n"
         "Es lo que más le ahorra al agente: sin esto redescubre la estructura en\n"
         "cada consulta.\n";
  return out.str();
}

bool project_contract_write(const std::string& workspace_root, const std::string& body,
                            bool force, std::string* path_out, std::string* err) {
  if (workspace_root.empty()) {
    if (err) {
      *err = "sin workspace abierto";
    }
    return false;
  }
  const fs::path path = fs::path(workspace_root) / "AGENTS.md";
  if (path_out) {
    *path_out = path.string();
  }
  std::error_code ec;
  if (!force && fs::exists(path, ec)) {
    if (err) {
      *err = "ya existe " + path.string() + " (edítalo, o /init --force para rehacerlo)";
    }
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + path.string();
    }
    return false;
  }
  out << body;
  if (!out) {
    if (err) {
      *err = "escritura incompleta en " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace tuide
