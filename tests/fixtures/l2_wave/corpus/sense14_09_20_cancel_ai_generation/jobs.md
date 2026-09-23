### Trabajo 1
consulta: dónde se captura la tecla Escape cuando la IA está generando código
keep: M2 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído es una función de escape de caracteres para logs ai trace escape, que no tiene relación con eventos de teclado
Abierto:
- key event handler
- on key press

### Trabajo 2
consulta: dónde se cancela la generación de código sin dejar el archivo a medias
keep: M3 M4
leído: stop completion server, cancel level1, stop owned unlocked
Cerrado:
leído: stop completion server, cancel level1, stop owned unlocked
Abierto:
- start server

### Trabajo 3
consulta: dónde se captura el clic fuera o la tecla Escape para detener la generación de la IA
keep: M4 M7
leído: handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape ni del clic fuera para detener la generación; lo leído es un manejador de entrada de texto de consola handle user input que solo detecta comandos escritos como cancel, cancel o cancelar mediante is cancel input, sin procesar eventos de teclado Key Down ni ratón MouseDown OutsideClick
Abierto:
- keydown handler
- mouse click handler
- ai generation cancel event


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: stop completion server, cancel level1, stop owned unlocked
    extra: Llama Backend, cancel all

T3
    visto: handle user input, is cancel input
    extra: clear pending insert, run insert async

entre abiertas:
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  handle user input → ai trace escape
  T2=>T3  handle user input → handle route → cancel current → cancel all → cancel level1
hacia el resto:
  T2 → Llama Backend
  T2 → cancel all
  T3 → clear pending insert
  T3 → run insert async
