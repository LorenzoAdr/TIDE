### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M2
leído: push active file diagnostics from cache, collect diagnostic decorations, apply decoration, Render Editor Line, render rich line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de marcado en el margen gutter; lo leído muestra que los errores se marcan como decoraciones en el contenido de la línea subrayado y color mediante collect diagnostic decorations y apply decoration pero no hay lógica para pintar iconos o marcas en el gutter margen izquierdo
Abierto:
- gutter renderer
- line number decoration
- gutter marker for diagnostic

### Trabajo 2
consulta: dónde se pintan las marcas en el margen izquierdo del editor para los diagnósticos
keep: M1 M2
leído: line diagnostic marker from map, line gutter marker, Make Editor Panel, diagnostics for editor line, apply lsp diagnostics to panel, handle gutter marker click
Cerrado:
encontré el mecanismo de marcado en el margen: line gutter marker decide el carácter o W basándose en line diagnostic marker from map, que consulta diagnostics for editor line; el pintado ocurre en el bucle de renderizado de Make Editor Panel que itera sobre las líneas visibles y llama a line gutter marker para cada una
Abierto:
- render loop gutter

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola inferior
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola inferior
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, collect diagnostic decorations, apply decoration, Render Editor Line
    extra: apply lsp diagnostics to panel, ISymbol Provider

T2
    visto: line diagnostic marker from map, line gutter marker, Make Editor Panel, diagnostics for editor line
    extra: Make Main Layout, git line changed

T3
    visto: (nada)

entre abiertas:
  T1=>T2  Make Editor Panel → push active file diagnostics from cache
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → Make Main Layout
  T2 → git line changed
