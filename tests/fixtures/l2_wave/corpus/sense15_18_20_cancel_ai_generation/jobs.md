### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape y cómo se limpia el archivo
keep: M5 M8
leído: cancel current, clear pending insert, handle user input, handle route, is cancel input, cancel all
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que la cancelación se activa por comandos de texto cancel, cancel, cancelar en handle user input, no por eventos de teclado. Tampoco se lee cómo se limpia el archivo solo se limpia el estado pendiente en memoria con clear pending insert .
Abierto:
- key event handler
- on key press
- file cleanup on cancel

### Trabajo 2
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M3
leído: ai trace escape, handle completion keys, handle editor keys, cancel level1, cancel all
Cerrado:
leído: ai trace escape, handle completion keys, handle editor keys, cancel level1, cancel all
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo cuando se cancela la generación
keep: M4 M8
leído: cancel current, clear pending insert, cancel all, begin insert at, handle user input, run insert async
Cerrado:
leído: cancel current, clear pending insert, cancel all, begin insert at, handle user input, run insert async
Abierto:
- handle route
- begin thinking
- end thinking


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, clear pending insert, handle user input, handle route
    extra: run insert async, handle level2 harness

T2
    visto: ai trace escape, handle completion keys, handle editor keys, cancel level1
    extra: Character, event is completion

T3
    visto: cancel current, clear pending insert, cancel all, begin insert at
    extra: handle route, Ai Controller
    entre interno: begin insert at → run insert async

entre abiertas:
  T1=>T2  mismo objeto: cancel all
  T1=>T2  cancel current → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle route → run level1 async → begin insert at
  T2=>T3  mismo objeto: cancel all
  T2=>T3  cancel current → ai trace escape
hacia el resto:
  T1 → handle level2 harness
  T1 → background
  T2 → Character
  T2 → event is completion
  T3 → Ai Controller
  T3 → Symbol Filter Runner
