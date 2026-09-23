### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M3
leído: cancel level1, handle editor escape, handle editor keys
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que capture Escape o clic fuera para cancelar la IA; lo leído muestra que handle editor escape solo gestiona UI del editor completado, búsqueda, cursores y cancel level1 es el efecto de cancelación pero no está conectado a ningún handler de eventos en el código leído
Abierto:
- handle editor mouse
- handle editor keys

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA
keep: M1
leído: cancel current, clear pending insert, cancel all, busy, cancel level1, begin insert at, handle user input, handle route
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que limpie el archivo o deshaga la inserción parcial al cancelar la IA; lo leído muestra que cancel current solo limpia el estado en memoria pending insert, insert anchor y cancela la generación cancel level1, sin tocar el sistema de archivos
Abierto:
- build insert pack

### Trabajo 3
consulta: dónde se revierte el contenido del archivo al cancelar la generación de la IA
keep: M1
leído: cancel current, clear pending insert
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que revierta el contenido del archivo al cancelar la IA; lo leído muestra que cancel current y clear pending insert solo limpian estado en memoria pending insert, insert anchor y notifican a la UI, sin tocar el buffer del editor ni el archivo en disco
Abierto:
- handle user input
- handle route
- revert insertion
- revert git diff block
- apply insert to buffer


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, handle editor escape, handle editor keys
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all, busy
    extra: Ai Controller, Symbol Filter Runner
    entre interno: begin insert at → run insert async

T3
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T3  cancel current → cancel all → cancel level1
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → Ai Controller
  T1 → clear primary selection
  T2 → Ai Controller
  T2 → Symbol Filter Runner
