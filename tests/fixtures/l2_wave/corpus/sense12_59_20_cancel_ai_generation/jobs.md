### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape
keep: M1
leído: cancel inflight completion, cancel current, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mapeo de la tecla Escape; lo leído muestra que la cancelación se dispara por texto cancel o cancel en el input de consola is cancel input, no por un evento de teclado Escape
Abierto:
- send completion request
- handle route
- start async worker
- send cancel
- keymap escape
- on key escape

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: ai trace escape, clear hover if
Cerrado:
leído: ai trace escape, clear hover if
Abierto:
- handle problems scrollbar mouse

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3
leído: cancel level1, clear pending insert, cancel current, cancel all, begin insert at
Cerrado:
leído: cancel level1, clear pending insert, cancel current, cancel all, begin insert at
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel inflight completion, cancel current, handle user input, is cancel input
    extra: completions at, start async worker

T2
    visto: ai trace escape, clear hover if, cancel level1
    extra: handle problems scrollbar mouse, hover effects enabled

T3
    visto: cancel level1, clear pending insert, cancel current, cancel all
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  clear pending insert → pending insert → ai trace escape
hacia el resto:
  T1 → completions at
  T1 → start async worker
  T2 → handle problems scrollbar mouse
  T2 → hover effects enabled
  T3 → Ai Controller
  T3 → handle route
