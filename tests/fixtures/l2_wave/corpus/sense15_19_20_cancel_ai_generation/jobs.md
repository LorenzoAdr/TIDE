### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de código de la IA
keep: M2 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación; lo leído ai trace escape es una función de escape de caracteres para strings, no un handler de eventos de teclado
Abierto:
- key handler
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código de la IA
keep: M8 M1
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, cancel current, handle user input, Event
Cerrado:
encontré el mecanismo: la tecla Escape se captura en handle ai console keys rama Escape dentro del bloque ai workflow menu open o selección y el clic fuera se gestiona en handle console panel mouse detectando clics fuera de la zona de input terminal para limpiar selecciones o desactivar menús . Ambos invocan a cancel current o clear pending insert para abortar la generación.
Abierto:
- handle route
- ai workflow menu open

### Trabajo 3
consulta: qué hace cancel current o clear pending insert al abortar la generación de la IA
keep: M1 M6
leído: clear pending insert, cancel current, cancel all, busy, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: cancel current bifurca según el estado. Si hay una inserción pendiente y no está ocupado, llama a clear pending insert que resetea pending insert e insert anchor y despierta la UI . Si está ocupado, llama a cancel all, que a su vez llama a cancel level1 setea agent cancel a true y cancela las tareas activas en tasks
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T3
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle ai console keys → handle user input → ai trace escape
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → handle route
  T3 → Ai Controller
