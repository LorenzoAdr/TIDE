### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M4 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído es una función de escape de caracteres para logs ai trace escape y el mecanismo de cancelación ai controller, pero falta el handler de input que dispara la cancelación
Abierto:
- handle escape
- on key press
- input handler

### Trabajo 2
consulta: dónde se detecta el clic fuera para cancelar la generación de la IA
keep: M1 M8
leído: clickable interaction cpp, quit confirm cpp
Cerrado:
leído: clickable interaction cpp, quit confirm cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se invoca la cancelación del controlador de IA al pulsar Escape
keep: M1 M6
leído: cancel all, cancel level1, Ai Controller
Cerrado:
leído: cancel all, cancel level1, Ai Controller
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: clickable interaction cpp, quit confirm cpp

T3
    visto: cancel all, cancel level1, Ai Controller
    extra: Symbol Filter Runner, join agent thread

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  cancel all → ai trace escape
  T2=>T3  sin camino
hacia el resto:
  T3 → Symbol Filter Runner
  T3 → join agent thread
