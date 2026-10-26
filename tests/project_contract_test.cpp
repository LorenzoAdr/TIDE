// Test de /init: detección del sistema de build, de la indentación real y de
// un fichero de estilo que miente; y que nunca pisa un AGENTS.md existente.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ai/project_contract.hpp"

namespace fs = std::filesystem;
using tuide::ProjectProbe;
using tuide::project_contract_template;
using tuide::project_contract_write;
using tuide::project_probe;

static int failures = 0;

static void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

static void write_file(const fs::path& path, const std::string& body) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << body;
}

static bool has(const std::vector<std::string>& v, const std::string& needle) {
  for (const auto& s : v) {
    if (s.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

static fs::path fresh(const std::string& name) {
  const fs::path p = fs::temp_directory_path() / ("tuide_init_" + name);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p;
}

int main() {
  // ---- CMake con binarios de test sueltos (la forma de este repo) ----
  {
    const fs::path root = fresh("cmake");
    write_file(root / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n"
               "add_executable(demo src/main.cpp)\nadd_executable(foo_test tests/foo_test.cpp)\n");
    write_file(root / "src" / "main.cpp", "int main() {\n  return 0;\n}\n");
    write_file(root / "third_party" / "dep" / "dep.cpp", "// vendored\n");
    const ProjectProbe p = project_probe(root.string());
    expect(has(p.kinds, "cmake"), "detecta cmake");
    expect(has(p.build_cmds, "cmake --build build"), "comando de build de cmake");
    expect(has(p.test_cmds, "_test"), "test por binario suelto, sin inventar ctest");
    expect(!has(p.test_cmds, "ctest"), "no propone ctest si no hay add_test");
    expect(has(p.excluded_dirs, "third_party"), "third_party como zona excluida");
    expect(has(p.languages, "C++"), "lenguaje dominante C++");
    const std::string body = project_contract_template(p);
    expect(body.find("## Tests") != std::string::npos, "plantilla con sección de tests");
    expect(body.find("TODO") != std::string::npos, "plantilla marca lo que falta");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- CMake con ctest de verdad ----
  {
    const fs::path root = fresh("ctest");
    write_file(root / "CMakeLists.txt",
               "project(demo)\nenable_testing()\nadd_test(NAME t COMMAND t)\n");
    const ProjectProbe p = project_probe(root.string());
    expect(has(p.test_cmds, "ctest"), "con enable_testing propone ctest");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- Script envoltorio: manda sobre el comando genérico ----
  {
    const fs::path root = fresh("wrapper");
    write_file(root / "CMakeLists.txt", "project(demo)\n");
    write_file(root / "compile.sh", "#!/bin/sh\nexec cmake --build build\n");
    const ProjectProbe p = project_probe(root.string());
    expect(!p.build_cmds.empty() && p.build_cmds.front() == "./compile.sh",
           "el envoltorio va primero");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- npm: solo los scripts que existen ----
  {
    const fs::path root = fresh("npm");
    write_file(root / "package.json",
               R"({"name":"x","scripts":{"build":"tsc","test":"vitest"}})");
    write_file(root / "yarn.lock", "");
    write_file(root / "src" / "a.ts", "export const a = 1;\n");
    const ProjectProbe p = project_probe(root.string());
    expect(has(p.build_cmds, "yarn build"), "usa yarn por el lockfile");
    expect(has(p.test_cmds, "yarn test"), "test de package.json");
    expect(!has(p.run_cmds, "dev"), "no inventa un script dev que no existe");
    expect(has(p.languages, "TypeScript"), "detecta TypeScript");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- cargo / go ----
  {
    const fs::path root = fresh("cargo");
    write_file(root / "Cargo.toml", "[package]\nname = \"x\"\n");
    const ProjectProbe p = project_probe(root.string());
    expect(has(p.test_cmds, "cargo test"), "cargo test");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- tareas de .tuide/config.json: son la verdad del proyecto ----
  {
    const fs::path root = fresh("tasks");
    write_file(root / "CMakeLists.txt", "project(demo)\n");
    write_file(root / ".tuide" / "config.json",
               R"({"ai":{"tasks":{"compile":{"command":"ninja -C out"},
                                  "test":{"command":"out/all_tests"}}}})");
    const ProjectProbe p = project_probe(root.string());
    expect(has(p.build_cmds, "ninja -C out"), "tarea de build del config");
    expect(has(p.test_cmds, "out/all_tests"), "tarea de test del config");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- indentación medida vs .clang-format que miente ----
  {
    const fs::path root = fresh("indent");
    write_file(root / ".clang-format", "IndentWidth: 4\nUseTab: ForIndentation\n");
    for (int i = 0; i < 5; ++i) {
      write_file(root / "src" / ("f" + std::to_string(i) + ".cpp"),
                 "void f() {\n  int a = 1;\n  if (a) {\n    return;\n  }\n}\n");
    }
    const ProjectProbe p = project_probe(root.string());
    expect(p.indent_width == 2 && !p.indent_tabs,
           "mide 2 espacios, got " + std::to_string(p.indent_width));
    expect(p.style_file_disagrees, "detecta que .clang-format no coincide");
    const std::string body = project_contract_template(p);
    expect(body.find("2 espacios") != std::string::npos, "la plantilla dice la indentación real");
    expect(body.find(".clang-format") != std::string::npos, "y avisa del fichero de estilo");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- escritura: nunca pisa, salvo --force ----
  {
    const fs::path root = fresh("write");
    std::string path;
    std::string err;
    expect(project_contract_write(root.string(), "primero\n", false, &path, &err),
           "escribe si no existe: " + err);
    expect(fs::exists(root / "AGENTS.md"), "AGENTS.md creado");
    expect(!project_contract_write(root.string(), "segundo\n", false, &path, &err),
           "no pisa un AGENTS.md existente");
    expect(err.find("--force") != std::string::npos, "el error dice cómo forzarlo");
    {
      std::ifstream in(root / "AGENTS.md");
      std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      expect(body == "primero\n", "el contenido original sigue intacto");
    }
    expect(project_contract_write(root.string(), "segundo\n", true, &path, &err),
           "con force sí rehace");
    expect(!project_contract_write("", "x", true, &path, &err), "sin workspace falla");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // ---- proyecto sin marcadores: no inventa nada ----
  {
    const fs::path root = fresh("bare");
    write_file(root / "notas.txt", "hola\n");
    const ProjectProbe p = project_probe(root.string());
    expect(p.build_cmds.empty() && p.test_cmds.empty(), "sin marcadores no hay comandos");
    const std::string body = project_contract_template(p);
    expect(body.find("TODO: comando de build") != std::string::npos,
           "la plantilla pide el build en vez de adivinar");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "project_contract_test OK\n";
  return 0;
}
