### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen izquierdo del editor
keep: M11 M4
leído: push active file diagnostics from cache, collect diagnostic decorations, Render Editor Line, line gutter marker, apply decoration
Cerrado:
leído: push active file diagnostics from cache, collect diagnostic decorations, Render Editor Line, line gutter marker, apply decoration
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la salida de la consola
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la salida de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, collect diagnostic decorations, Render Editor Line, line gutter marker
    extra: apply lsp diagnostics to panel, ISymbol Provider

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → apply lsp diagnostics to panel
  T1 → ISymbol Provider
