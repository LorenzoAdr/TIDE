### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la IA
keep: M1
leído: handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la IA; lo leído muestra que la cancelación se realiza exclusivamente mediante comandos de texto cancel, cancel, cancelar en la entrada de línea de texto handle user input - is cancel input . No hay evidencia de manejo de eventos de teclado Escape o ratón clic fuera en el código revisado.
Abierto:
- event handler
- on key press
- on mouse click
- ui event loop

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6 M10
leído: cancel level1, cancel current, clear pending insert, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que limpia el archivo a medias al cancelar la generación; lo leído muestra que la cancelación de la inserción pendiente clear pending insert solo resetea banderas de estado pending insert, insert anchor y despierta el ciclo, sin invocar ninguna operación de edición de texto borrado, undo, revert sobre el buffer del editor
Abierto:
- handle user input
- handle route
- delete range
- undo insert
- on cancel effect


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle user input, is cancel input
    extra: clear pending insert, run insert async

T2
    visto: cancel level1, cancel current, clear pending insert, begin insert at
    extra: cancel all, Ai Controller

entre abiertas:
  T1=>T2  handle user input → handle route → cancel current → cancel all → cancel level1
hacia el resto:
  T1 → run insert async
  T2 → cancel all
  T2 → Ai Controller
