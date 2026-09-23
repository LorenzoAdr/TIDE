### Trabajo 1
consulta: cómo se cancela la generación de código cuando se pulsa Escape o se hace clic fuera
keep: M1 M3
leído: handle editor keys, cancel completion fetch, cancel live lsp on cursor move, handle editor mouse, completion lsp tick, close, end mouse selection
Cerrado:
leído: handle editor keys, cancel completion fetch, cancel live lsp on cursor move, handle editor mouse, completion lsp tick, close, end mouse selection
Abierto:
- handle editor escape

### Trabajo 2
consulta: dónde se cancela la generación de código de la IA cuando se pulsa Escape o se hace clic fuera
keep: M1 M7
leído: cancel current, cancel level1, handle user input, handle route, is cancel input, cancel all
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído muestra que la cancelación se activa únicamente por comandos de texto cancel, cancel, cancelar en el input handler, sin detección de eventos de teclado o ratón
Abierto:
- handle key event
- handle mouse event
- on escape
- on click outside


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle editor keys, cancel completion fetch, cancel live lsp on cursor move, handle editor mouse
    extra: active tab large virtual view, active tab read only

T2
    visto: cancel current, cancel level1, handle user input, handle route
    extra: clear pending insert, Ai Controller

entre abiertas:
  T1=>T2  handle user input → handle route → run level1 async → Make Main Layout → Make Editor Panel → handle editor keys
hacia el resto:
  T1 → active tab large virtual view
  T1 → active tab read only
  T2 → clear pending insert
  T2 → Ai Controller
