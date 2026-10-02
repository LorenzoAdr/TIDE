### Trabajo 1
consulta: dónde se parsean los errores de compilación de la consola inferior
keep: M9 M11
leído: Make Diagnostics Panel, build rows, supports diagnostics, diagnostics display allowed, diagnostics reveal allowed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea los errores de compilación desde la consola inferior; lo leído confirma que el panel de diagnósticos consume datos estructurados vía LSP ISymbol Provider no texto plano de la terminal. La consulta asume un parsing de stdout stderr de la consola, pero el sistema usa LSP para obtener diagnósticos.
Abierto:
- parse output
- on data

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1 M6
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Cerrado:
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Abierto:
- handle editor mouse


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Make Diagnostics Panel, build rows, supports diagnostics, diagnostics display allowed
    extra: Make Console Panel, Make Main Layout

T2
    visto: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Console Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout
