### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación de la IA; lo leído es una función de escape de caracteres en strings ai trace escape, no un handler de eventos de teclado
Abierto:
- key handler
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se cancela la generación de la IA cuando el usuario pulsa Escape
keep: M5
leído: cancel current, cancel level1, handle user input, is cancel input
Cerrado:
leído: cancel current, cancel level1, handle user input, is cancel input
Abierto:
- handle route

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
encontré dónde se limpia el archivo a medias al cancelar la generación de la IA: en clear pending insert llamado desde cancel current cuando pending insert es true y no hay tarea busy, que resetea pending insert y insert anchor si hay tarea busy, cancel all delega a cancel level1 y tasks cancel para detener la generación en curso, pero la limpieza del buffer pendiente ocurre específicamente en clear pending insert
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, cancel level1, handle user input, is cancel input
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
  T2 → handle route
  T2 → Ai Controller
  T3 → handle route
  T3 → Ai Controller
