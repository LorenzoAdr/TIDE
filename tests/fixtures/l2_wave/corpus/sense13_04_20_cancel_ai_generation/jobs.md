### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la IA
keep: M8
leído: handle editor escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la interrupción de la IA; lo leído handle editor escape solo cierra UI completado, find, modales y no toca el estado de la IA
Abierto:
- handle editor keys

### Trabajo 2
consulta: dónde se cancela la generación de código para evitar dejar el archivo a medias
keep: M5
leído: cancel level1, cancel all, tasks cancel, clear pending insert
Cerrado:
leído: cancel level1, cancel all, tasks cancel, clear pending insert
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle editor escape
    extra: handle editor keys, clear primary selection

T2
    visto: cancel level1, cancel all, tasks cancel, clear pending insert
    extra: Ai Controller, Symbol Filter Runner

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → handle editor keys
  T1 → clear primary selection
  T2 → Ai Controller
  T2 → Symbol Filter Runner
