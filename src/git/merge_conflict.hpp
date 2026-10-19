#pragma once

#include <string>
#include <vector>

namespace tuide {

enum class ConflictSide {
  kOurs,
  kBase,
  kTheirs,
};

// Rango de un bloque de conflicto de merge dentro de un buffer de líneas.
// Todos los índices son 0-based; los *_end son exclusivos (estilo [begin, end)).
struct ConflictHunk {
  int start_line = 0;  // línea del marcador "<<<<<<<"
  int end_line = 0;    // línea del marcador ">>>>>>>"

  int ours_begin = 0;
  int ours_end = 0;

  bool has_base = false;  // true si el archivo usa el estilo diff3 (marcador "|||||||")
  int base_begin = 0;
  int base_end = 0;

  int theirs_begin = 0;
  int theirs_end = 0;

  std::string ours_label;
  std::string base_label;
  std::string theirs_label;
};

struct ConflictParseResult {
  std::vector<ConflictHunk> hunks;

  bool has_conflicts() const { return !hunks.empty(); }
};

// Escanea `lines` buscando bloques delimitados por "<<<<<<<" / "|||||||" (opcional,
// estilo diff3/zdiff3) / "=======" / ">>>>>>>". Un bloque sin su marcador de cierre
// se descarta en vez de reportarse a medias.
ConflictParseResult parse_conflict_markers(const std::vector<std::string>& lines);

// Contenido de un lado del hunk (sin marcadores), listo para sustituir el bloque.
// Para kBase devuelve {} si el hunk no tiene sección base.
std::vector<std::string> conflict_hunk_side_lines(const std::vector<std::string>& lines,
                                                  const ConflictHunk& hunk, ConflictSide side);

// "Aceptar ambos": contenido de "ours" seguido del de "theirs".
std::vector<std::string> conflict_hunk_both_lines(const std::vector<std::string>& lines,
                                                  const ConflictHunk& hunk);

// Índice (en `hunks`) del primer hunk cuyo inicio esté después de `current_line`;
// da la vuelta al principio si no hay ninguno. -1 si `hunks` está vacío.
int find_next_conflict_hunk(const std::vector<ConflictHunk>& hunks, int current_line);

// Igual que find_next_conflict_hunk pero hacia atrás (da la vuelta al final).
int find_prev_conflict_hunk(const std::vector<ConflictHunk>& hunks, int current_line);

// Índice del hunk que contiene `line`, o -1 si ninguno lo contiene.
int conflict_hunk_at_line(const std::vector<ConflictHunk>& hunks, int line);

enum class ConflictLineBand {
  kNone,
  kOurs,    // incluye la línea del marcador "<<<<<<<"
  kBase,    // incluye la línea del marcador "|||||||" (solo diff3)
  kTheirs,  // incluye las líneas de los marcadores "=======" y ">>>>>>>"
};

// Clasifica `line` dentro de la banda visual del hunk que la contiene (o kNone).
// Útil para decorar el editor con un color de fondo distinto por lado del conflicto.
ConflictLineBand conflict_line_band(const std::vector<ConflictHunk>& hunks, int line);

}  // namespace tuide
