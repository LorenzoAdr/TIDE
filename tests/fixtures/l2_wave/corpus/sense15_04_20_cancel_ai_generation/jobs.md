### Trabajo 1
consulta: dónde se cancela la generación de código cuando se pulsa Escape o se hace clic fuera
keep: M2 M7
leído: cancel current, cancel level1, handle user input, is cancel input, cancel all, clear search input focus
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído muestra que la cancelación se dispara únicamente por texto cancel en handle user input, y clear search input focus solo gestiona el foco sin invocar cancelación
Abierto:
- handle route
- handle key event
- handle mouse click
- on escape

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7 M8
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, Event, cancel current, handle user input
Cerrado:
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, Event, cancel current, handle user input
Abierto:
- handle route

### Trabajo 3
consulta: dónde se invoca la cancelación al pulsar Escape o hacer clic fuera durante la generación
keep: M4 M7
leído: cancel current, handle ai console keys, handle console panel mouse, handle user input, handle route
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera durante la generación; lo leído muestra que handle ai console keys gestiona Escape solo para limpiar selecciones de terminal clear terminal selection y que handle console panel mouse gestiona clics para activar inputs o abrir resultados, pero ninguno invoca cancel current ni cancela la generación en curso
Abierto:
- cancel level1
- on key event


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, cancel level1, handle user input, is cancel input
    extra: handle route, clear pending insert

T2
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T3
    visto: cancel current, handle ai console keys, handle console panel mouse, handle user input
    extra: clear pending insert, cancel all

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T3  mismo objeto: handle ai console keys
  T2=>T3  handle route → cancel current → clear pending insert
hacia el resto:
  T1 → Ai Controller
  T1 → run insert async
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → ai tab active
  T3 → event is ctrl c
