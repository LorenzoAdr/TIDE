### Trabajo 1
consulta: dónde se pintan las marcas en el margen izquierdo del editor
keep: M1 M4
leído: handle git gutter click, handle gutter marker click, build git marks snapshot
Cerrado:
leído: handle git gutter click, handle gutter marker click, build git marks snapshot
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola inferior
keep: M9 M11
leído: Make Diagnostics Panel, build rows, supports diagnostics
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de parsing de errores de compilación desde la consola inferior; lo leído confirma que el panel de diagnósticos diagnostics panel consume exclusivamente datos de LSP vía ISymbol Provider, y las búsquedas de funciones de parsing de texto plano parse en el grafo retornaron 0 hits.
Abierto:
- parse console errors
- parse build output
- on output


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle git gutter click, handle gutter marker click, build git marks snapshot
    extra: handle editor mouse, previous line content

T2
    visto: Make Diagnostics Panel, build rows, supports diagnostics
    extra: Make Console Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → previous line content
  T2 → Make Console Panel
  T2 → Make Main Layout
