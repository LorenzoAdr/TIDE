### Trabajo 1
consulta: dónde se captura la tecla Escape durante la generación de código
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape durante la generación de código
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape durante la generación de código; lo leído es una función de escape de caracteres JSON ai trace escape que procesa strings, no eventos de teclado.
Abierto:
- handle key event
- on key down
- escape key handler

### Trabajo 2
consulta: dónde se cancela la generación de código y se limpia el archivo
keep: M8
leído: cancel level1, cancel current, cancel all, clear pending insert, clear ai session, clear session
Cerrado:
encontré dónde se cancela la generación de código y se limpia el archivo: la cancelación se gestiona en cancel current que llama a cancel all para detener el agente y la limpieza de archivos de sesión ocurre en clear session invocada por clear ai session . clear session usa remove all para borrar el directorio de la sesión L2 en disco.
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M8
leído: cancel current, cancel level1, handle user input, handle route, is cancel input, cancel all
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la cancelación por Escape o clic fuera; lo leído es cancelación por texto cancel en el input de consola
Abierto:
- on key event
- on mouse click
- focus lost handler

### Trabajo 4
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel level1, cancel current, cancel all, clear pending insert
    extra: Ai Controller, handle user input

T3
    visto: cancel current, cancel level1, handle user input, handle route
    extra: clear pending insert, Ai Controller

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  handle user input → clear pending insert
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → Ai Controller
  T3 → run insert async
