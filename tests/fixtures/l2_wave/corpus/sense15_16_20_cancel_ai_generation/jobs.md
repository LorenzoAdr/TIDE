### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape
keep: M4 M1
leído: cancel current, handle user input, is cancel input, cancel all
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que la cancelación se activa por texto cancel, cancel, cancelar en el input de línea, no por el evento de teclado Escape
Abierto:
- handle route
- handle key event
- on key press
- key handler

### Trabajo 2
consulta: dónde se cancela la generación de código al hacer clic fuera del área de edición
keep: M2 M9
leído: cancel live lsp on cursor move, clear suggestion cache
Cerrado:
leído: cancel live lsp on cursor move, clear suggestion cache
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo cuando se cancela la generación de código
keep: M6 M12
leído: cancel current, cancel level1, clear pending insert, cancel all
Cerrado:
leído: cancel current, cancel level1, clear pending insert, cancel all
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, handle user input, is cancel input, cancel all
    extra: handle route, clear pending insert

T2
    visto: cancel live lsp on cursor move, clear suggestion cache
    extra: completion lsp tick, ISymbol Provider

T3
    visto: cancel current, cancel level1, clear pending insert, cancel all
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle user input → handle route → run level1 async → Make Main Layout → Make Editor Panel → completion lsp tick
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T3  sin camino
hacia el resto:
  T1 → handle route
  T1 → run insert async
  T2 → completion lsp tick
  T2 → ISymbol Provider
  T3 → handle route
  T3 → Ai Controller
