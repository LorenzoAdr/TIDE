### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
keep: M2
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para interrumpir la generación; lo leído es una función de escape de caracteres para logs ai trace escape, no un handler de eventos de UI
Abierto:
- stop ai
- on key press
- on mouse click
- cancel generation

### Trabajo 2
consulta: dónde se cancela la inserción de código en el archivo para evitar dejarlo a medias
keep: M6
leído: clear pending insert, begin insert at, cancel current, handle user input, is cancel input
Cerrado:
encontré dónde se cancela la inserción: en handle user input se detecta cancel vía is cancel input y llama a clear pending insert, que limpia pending insert e insert anchor para evitar dejar el archivo a medias
Abierto:
- handle route

### Trabajo 3
consulta: dónde se captura el clic fuera o la pérdida de foco para cancelar la generación
keep: M1 M8
leído: handle console panel mouse, activate console input, cancel current, handle user input, is cancel input
Cerrado:
leído: handle console panel mouse, activate console input, cancel current, handle user input, is cancel input
Abierto:
- handle console tab click
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: clear pending insert, begin insert at, cancel current, handle user input
    extra: handle route, on ai tab opened

T3
    visto: handle console panel mouse, activate console input, cancel current, handle user input
    extra: Is Empty, app tab active

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  handle console panel mouse → cancel current → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle console panel mouse → cancel current → clear pending insert
hacia el resto:
  T2 → handle route
  T2 → on ai tab opened
  T3 → Is Empty
  T3 → app tab active
