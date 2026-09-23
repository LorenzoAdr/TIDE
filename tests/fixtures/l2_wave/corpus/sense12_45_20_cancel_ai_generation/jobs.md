### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M2
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación de la IA; lo leído es una función de escape de caracteres para strings JSON ai trace escape
Abierto:
- handle ai console keys
- cancel generation

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación
keep: M8
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M8
leído: visual highlight selection key from, cancel current, handle user input, handle route, is cancel input
Cerrado:
leído: visual highlight selection key from, cancel current, handle user input, handle route, is cancel input
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se detecta la pulsación de Escape o el clic fuera para cancelar la generación
keep: M8
leído: handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de la tecla Escape ni del clic fuera para cancelar la generación; lo leído es la detección de comandos de texto cancel, cancel, cancelar en el input de la consola
Abierto:
- handle key event
- on mouse click
- ui event loop


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: visual highlight selection key from, cancel current, handle user input, handle route
    extra: tick visual highlight scheduler, Make Editor Panel

T4
    visto: handle user input, is cancel input
    extra: clear pending insert, run insert async

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  cancel current → ai trace escape
  T1=>T4  handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle user input → clear pending insert
  T2=>T4  handle user input → handle route → cancel current
  T3=>T4  mismo objeto: handle user input
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → tick visual highlight scheduler
  T3 → Make Editor Panel
  T4 → run insert async
