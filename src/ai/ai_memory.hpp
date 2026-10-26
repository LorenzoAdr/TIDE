#pragma once

// Memoria durable del agente: `.tuide/ai/memory.md`.
//
// Hechos que el usuario le dice una vez y no quiere repetir en cada consulta
// ("usa siempre ./compile.sh", "el panel AI vive en console_panel.cpp"). Se
// carga en el prefijo inmutable del prompt, junto al contrato del proyecto, y
// cuenta como evidencia por el mismo motivo: lo escribió el humano.
//
// La escribe el usuario con `/remember`, no el agente. El experimento del
// contrato (docs/plans/ai-harness-v2-plan.md, M4.1) mostró que lo que paga es
// el hecho escrito por quien conoce el repo; una memoria que el agente se
// autoescribe necesita un filtro de calidad que todavía no existe.

#include <string>
#include <vector>

namespace tuide {

// Tope de la memoria inyectada en el prompt. Se conservan los hechos más
// recientes: la memoria vieja no puede desplazar al contrato ni al notebook.
inline constexpr int kAiMemoryPromptChars = 2000;
inline constexpr int kAiMemoryFactChars = 300;
inline constexpr int kAiMemoryMaxItems = 60;

std::string ai_memory_path(const std::string& workspace_root);

// Hechos en orden de escritura (el más antiguo primero).
std::vector<std::string> ai_memory_items(const std::string& workspace_root);

// Bloque para el prompt (vacío si no hay memoria). Recorta por el principio:
// si no cabe todo, se van los hechos más antiguos.
std::string ai_memory_load(const std::string& workspace_root,
                           int max_chars = kAiMemoryPromptChars);

// Añade un hecho. Ignora duplicados exactos y recorta a kAiMemoryFactChars.
bool ai_memory_add(const std::string& workspace_root, const std::string& fact, std::string* err);

// Borra el hecho número `index_1based` tal como lo numera `/memory`.
bool ai_memory_remove(const std::string& workspace_root, int index_1based, std::string* err);

}  // namespace tuide
