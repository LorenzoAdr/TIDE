### Trabajo 1
consulta: dónde se captura Escape o clic fuera para cancelar la generación de código
keep: M8
leído: cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape o clic fuera para cancelar la generación de código; lo leído muestra que la cancelación se realiza exclusivamente vía texto cancel, cancel, cancelar en el controlador de entrada de texto is cancel input, sin interceptar eventos de teclado Escape ni ratón clic fuera en la capa de UI Editor
Abierto:
- handle key event
- on key
- click outside

### Trabajo 2
consulta: dónde se limpia el archivo a medias cuando se cancela la generación de código
keep: M8
leído: clear pending insert, cancel all
Cerrado:
leído: clear pending insert, cancel all
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se limpia el archivo a medias cuando se cancela la generación de código
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
encontré dónde se limpia el archivo a medias: en clear pending insert que es llamado por cancel current cuando pending insert es true y no hay tarea busy; la limpieza consiste en resetear el flag pending insert y el ancla insert anchor
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, cancel current, handle user input, handle route
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert, cancel all, cancel current
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  cancel all → cancel level1
  T1=>T3  mismo objeto: cancel level1
  T1=>T3  handle user input → clear pending insert
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → Ai Controller
  T1 → run insert async
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → Ai Controller
  T3 → Symbol Filter Runner
