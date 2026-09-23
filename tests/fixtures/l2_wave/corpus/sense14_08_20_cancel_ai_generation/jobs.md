### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído ai trace escape es una función de escape de caracteres para strings, no un handler de teclado
Abierto:
- handle key
- key handler
- on key press

### Trabajo 2
consulta: dónde se cancela la generación de código para no dejar el archivo a medias
keep: M1 M9
leído: cancel level1, cancel all, stop completion server
Cerrado:
leído: cancel level1, cancel all, stop completion server
Abierto:
- stop owned unlocked

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3 M4
leído: cancel level1, cancel current, cancel all, clear pending insert
Cerrado:
leído: cancel level1, cancel current, cancel all, clear pending insert
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel level1, cancel all, stop completion server
    extra: Ai Controller, Symbol Filter Runner

T3
    visto: cancel level1, cancel current, cancel all, clear pending insert
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  cancel level1 → ai trace escape
  T2=>T3  mismo objeto: cancel level1
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → Ai Controller
  T3 → handle user input
