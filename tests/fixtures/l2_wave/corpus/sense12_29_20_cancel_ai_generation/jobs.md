### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de Escape clic para cancelar la generación de IA; lo leído ai trace escape es una función de escape de strings JSON, no un handler de eventos de UI
Abierto:
- handle route
- json escape

### Trabajo 2
consulta: dónde se limpia o revierte el archivo que se está escribiendo a medias al cancelar la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, begin insert at, handle user input
Cerrado:
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, begin insert at, handle user input
Abierto:
- handle route

### Trabajo 3
consulta: dónde se limpia o revierte el archivo que se está escribiendo a medias al cancelar la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, begin insert at, handle user input, run insert async
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza reversión del archivo a medias al cancelar la IA; lo leído muestra que la cancelación solo limpia el estado de inserción pendiente pending insert, insert anchor y cancela la tarea de agente agent cancel, tasks cancel, pero no hay código que revertir el contenido del archivo en sí
Abierto:
- handle route
- run insert async body
- apply tool


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle route, Ai Controller

T3
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle route, Ai Controller
    entre interno: begin insert at → run insert async

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T2 → handle route
  T2 → Ai Controller
  T3 → handle route
  T3 → Ai Controller
