#pragma once

// Menciones `@fichero` en la línea del panel AI.
//
// "explícame @console_panel.cpp" ahorra toda la fase de localización: el
// usuario ya sabe dónde mirar. Se resuelven contra el árbol (no hace falta la
// ruta completa: basta un sufijo o el nombre), se adjunta un extracto, y van al
// prompt como evidencia aportada por el usuario — igual que el contrato y la
// memoria, y declaradas también en el prompt del verificador, porque si no
// pedir un `path:línea` para algo que el usuario ya señaló cuesta rondas.
//
// Solo ficheros. `@símbolo` no: para eso ya está el explorador, y adivinar
// entre un fichero y un símbolo con el mismo nombre daría sorpresas.

#include <string>
#include <vector>

namespace tuide {

// Presupuesto del bloque: va en el prefijo inmutable, así que se paga en cada
// llamada de la consulta.
inline constexpr int kAiMentionMaxFiles = 4;
inline constexpr int kAiMentionCharsEach = 2500;

struct AiMention {
  std::string token;    // lo que escribió el usuario, sin la @
  std::string path;     // ruta relativa al root, vacía si no se resolvió
  std::string excerpt;  // extracto del fichero (head), recortado
  bool truncated = false;
  int lines = 0;
  // Más de un candidato: se escoge el mejor y se avisa, para que el usuario
  // sepa por qué le llegó ese y no otro.
  std::vector<std::string> other_candidates;
};

// Extrae los `@token` del mensaje y los resuelve. Orden de preferencia: ruta
// relativa exacta, sufijo de ruta, nombre de fichero, y subcadena sin importar
// mayúsculas. Salta los mismos directorios de ruido que la búsqueda.
std::vector<AiMention> ai_resolve_mentions(const std::string& message,
                                           const std::string& workspace_root,
                                           int max_files = kAiMentionMaxFiles,
                                           int max_chars_each = kAiMentionCharsEach);

// Bloque para el prompt. Vacío si no hay menciones resueltas.
std::string ai_mentions_block(const std::vector<AiMention>& mentions);

// Resumen de una línea por mención para el panel ("· @x → src/x.cpp (120 líneas)").
std::vector<std::string> ai_mentions_report(const std::vector<AiMention>& mentions);

}  // namespace tuide
