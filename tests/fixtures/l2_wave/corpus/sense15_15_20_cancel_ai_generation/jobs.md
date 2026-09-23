### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M9
leído: ai trace escape, clear
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape ni el clic fuera para cancelar la generación; lo leído es una función de escape de caracteres de string ai trace escape y un método de limpieza de buffer clear, que no tienen relación con eventos de input o cancelación de tareas
Abierto:
- handle key event
- cancel level1
- on mouse click

### Trabajo 2
consulta: dónde se cancela la generación de la IA al pulsar Escape o hacer clic fuera
keep: M3 M1
leído: cancel current, cancel live lsp on cursor move, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape ni el clic fuera para cancelar la generación de la IA; lo leído es un sistema de cancelación basado en comandos de texto cancel, cancel, cancelar en el controlador de IA y una cancelación de LSP basada en movimiento del cursor, pero no hay handlers de teclado Escape ni de ratón clic fuera vinculados a la cancelación de la IA
Abierto:
- handle route
- handle key event
- on mouse click
- on escape
- on click outside

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6 M8
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, clear
    extra: cycle level2 mode override, set level2 mode override

T2
    visto: cancel current, cancel live lsp on cursor move, handle user input, is cancel input
    extra: handle route, clear pending insert

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle user input → clear pending insert
hacia el resto:
  T1 → cycle level2 mode override
  T1 → set level2 mode override
  T2 → handle route
  T2 → completion lsp tick
  T3 → handle route
  T3 → Ai Controller
