### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M3
leído: ai trace escape, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído muestra que la cancelación se activa mediante comandos de texto cancel, cancel, cancelar en is cancel input, no por un evento de teclado
Abierto:
- run level1 async
- handle key event
- on key press
- key event handler

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción al cancelar la IA
keep: M1 M4
leído: cancel current, clear pending insert, cancel all, cancel level1, begin insert at, handle user input, run insert async
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la limpieza de archivo o deshacer de inserción al cancelar; lo leído muestra que la cancelación solo limpia estado interno pending insert, insert anchor y cancela tareas L1 L2, pero no hay código que deshaga cambios en el buffer del editor ni elimine archivos temporales
Abierto:
- handle route
- editor undo on cancel
- temp file cleanup


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, handle user input, is cancel input
    extra: run level1 async, handle route

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle route, Ai Controller
    entre interno: begin insert at → run insert async

entre abiertas:
  T1=>T2  mismo objeto: handle user input
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → run level1 async
  T1 → handle route
  T2 → handle route
  T2 → Ai Controller
