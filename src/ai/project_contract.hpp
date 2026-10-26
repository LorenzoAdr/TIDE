#pragma once

// Generación del contrato de proyecto (`AGENTS.md`) que consume el piloto —
// ver admin_load_project_contract en ai/l2_admin.hpp.
//
// El agente llegaba a cada repo sin saber cómo se compila, cómo se testea ni
// qué no debe tocar, y lo redescubría consulta a consulta. Escribirlo a mano
// funciona (medido: una consulta que agotaba el timeout pasó a 2 llamadas),
// pero nadie empieza por ahí. `/init` deja un borrador con lo que SÍ se puede
// deducir del árbol y marca con TODO lo que solo sabe el humano.
//
// Reglas: no inventar comandos (si hay un script envoltorio en la raíz o tareas
// en `.tuide/config.json`, esos mandan), y nunca sobrescribir un AGENTS.md
// existente sin que lo pidan.

#include <string>
#include <vector>

namespace tuide {

struct ProjectProbe {
  std::string name;              // nombre del directorio raíz
  std::vector<std::string> kinds;  // cmake, make, npm, cargo, go, python…
  std::vector<std::string> build_cmds;
  std::vector<std::string> test_cmds;
  std::vector<std::string> run_cmds;
  // Directorios presentes que no son fuente del proyecto (vendored, artefactos,
  // copias de trabajo). Van a la sección de zonas prohibidas.
  std::vector<std::string> excluded_dirs;
  // Lenguajes dominantes por nº de ficheros, el primero el más frecuente.
  std::vector<std::string> languages;
  // Indentación dominante en las fuentes: ancho en espacios, o tabs.
  int indent_width = 0;
  bool indent_tabs = false;
  // true si `.clang-format`/`.editorconfig` declara algo distinto de lo medido:
  // el fichero de estilo miente y conviene decirlo en el contrato.
  bool style_file_disagrees = false;
  std::string style_file;
};

// Inspecciona el árbol. No ejecuta nada: solo lee marcadores y muestrea fuentes.
ProjectProbe project_probe(const std::string& workspace_root);

// Borrador de AGENTS.md a partir de la inspección.
std::string project_contract_template(const ProjectProbe& probe);

// Escribe `<root>/AGENTS.md`. Sin `force`, falla si ya existe (nunca pisa el
// contrato que haya escrito el usuario).
bool project_contract_write(const std::string& workspace_root, const std::string& body,
                            bool force, std::string* path_out, std::string* err);

}  // namespace tuide
