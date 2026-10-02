### Trabajo 1
consulta: cómo se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M5
leído: cancel current, cancel level1, handle user input, handle route, is cancel input, cancel all, clear pending insert, handle key event
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído muestra que la cancelación se dispara únicamente por input textual cancel en handle user input, y los manejadores de eventos de teclado ratón handle key event, handle mouse event no existen en ai controller ni se vinculan a cancel current
Abierto:
- on escape
- on click outside

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, cancel current, Event
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído muestra que handle ai console keys captura Escape solo para cerrar menús o limpiar selecciones sin llamar a cancel current, y handle console panel mouse no invoca cancelación ante clics fuera; cancel current se invoca únicamente desde handle user input y handle route
Abierto:
- handle route
- handle user input


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, cancel level1, handle user input, handle route
    extra: Ai Controller, run insert async

T2
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
hacia el resto:
  T1 → Ai Controller
  T1 → run insert async
  T2 → ai tab active
  T2 → event is ctrl c
