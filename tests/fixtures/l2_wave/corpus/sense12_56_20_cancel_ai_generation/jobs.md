### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído ai trace escape es una función de escape de caracteres para strings, no de input de teclado
Abierto:
- key handler
- on key press
- input loop

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación
keep: M4
leído: clear pending insert, cancel level1, cancel current, cancel all
Cerrado:
leído: clear pending insert, cancel level1, cancel current, cancel all
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se captura la tecla Escape o el clic fuera para invocar la cancelación de la generación
keep: M6
leído: handle ai console keys, cancel current, handle console panel mouse, handle user input
Cerrado:
leído: handle ai console keys, cancel current, handle console panel mouse, handle user input
Abierto:
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: clear pending insert, cancel level1, cancel current, cancel all
    extra: handle user input, handle route

T3
    visto: handle ai console keys, cancel current, handle console panel mouse, handle user input
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  handle ai console keys → handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle ai console keys → clear pending insert
hacia el resto:
  T2 → handle route
  T2 → Ai Controller
  T3 → has pending insert
  T3 → ai tab active
