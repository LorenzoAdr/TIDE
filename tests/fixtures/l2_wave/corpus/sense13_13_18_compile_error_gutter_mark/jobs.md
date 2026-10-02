### Trabajo 1
consulta: dónde se leen los errores de compilación del texto de la consola para marcarlos en el editor
keep: M7
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se analizan los errores de compilación del texto de la consola para crear marcadores en el editor
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se analizan los errores de compilación del texto de la consola para crear marcadores en el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crea la decoración de línea roja en el margen izquierdo del editor para marcar errores
keep: M10
leído: apply lsp diagnostics to panel, push active file diagnostics from cache, rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, fold gutter marker, line diagnostic marker from map
Cerrado:
leído: apply lsp diagnostics to panel, push active file diagnostics from cache, rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, fold gutter marker, line diagnostic marker from map
Abierto:
- handle fold gutter click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
    extra: ISymbol Provider, panel diagnostics match doc

T2
    visto: (nada)

T3
    visto: apply lsp diagnostics to panel, push active file diagnostics from cache, rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: rebuild diagnostics by line if changed
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → panel diagnostics match doc
  T3 → ISymbol Provider
  T3 → allows lsp ui
