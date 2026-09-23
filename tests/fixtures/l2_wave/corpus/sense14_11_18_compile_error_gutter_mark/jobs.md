### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M2 M3
leído: push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Cerrado:
encontré el mecanismo: los errores se marcan en el gutter mediante line gutter marker que delega en line diagnostic marker from map para devolver si hay un error de severidad k Error
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación del texto de la consola para generar marcadores
keep: M11 M12
leído: push active file diagnostics from cache
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el arranque del trabajo; lo leído formatea un string
Abierto:
- run job async

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para crear marcadores en el editor
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola para crear marcadores en el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker, gutter buffer line at row
    extra: ISymbol Provider, invalidate editor view

T2
    visto: push active file diagnostics from cache
    extra: apply lsp diagnostics to panel, ISymbol Provider

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: push active file diagnostics from cache
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → ISymbol Provider
  T2 → allows lsp ui
