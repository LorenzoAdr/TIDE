### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1 M4
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de dibujo visual de la línea roja en el margen; lo leído son funciones de reconstrucción de datos rebuild diagnostics by line que mapean diagnósticos a líneas, pero no contienen código de renderizado UI draw . Las búsquedas de símbolos visuales draw diagnostic gutter draw etc. dieron 0 hits.
Abierto:
- draw diagnostics
- render gutter
- paint margin

### Trabajo 2
consulta: dónde se aplican los diagnósticos de compilación al panel del editor para mostrar la línea roja
keep: M1 M5
leído: apply lsp diagnostics to panel, diagnostics for editor line
Cerrado:
leído: apply lsp diagnostics to panel, diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se convierten los errores de la consola en diagnósticos para el editor
keep: M5 M6
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se convierten los errores de la consola en diagnósticos para el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: apply lsp diagnostics to panel, diagnostics for editor line
    extra: ISymbol Provider, allows lsp ui

T3
    visto: (nada)

entre abiertas:
  T1=>T2  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → push active file diagnostics from cache
  T1 → ISymbol Provider
  T2 → ISymbol Provider
  T2 → allows lsp ui
