### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera cuando la IA está generando código
keep: M5 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera durante la generación; lo leído es una función de escape de strings para logs ai trace escape, que no tiene relación con eventos de UI
Abierto:
- key event handler
- mouse click handler

### Trabajo 2
consulta: dónde se cancela la generación de código de la IA para evitar dejar el archivo a medias
keep: M3 M10
leído: cancel current, cancel level1, clear pending insert, cancel all, handle user input, handle route, is cancel input, run insert async
Cerrado:
encontré el mecanismo de cancelación: la generación se cancela vía comando de texto cancel detectado en handle user input, que invoca cancel current; este llama a cancel all que setea agent cancel y cancela tasks o clear pending insert si es pendiente . No hay captura de Escape ni clic fuera en el código leído; la cancelación es textual. El efecto de no dejar el archivo a medias se logra abortando la tarea tasks cancel y limpiando el anchor clear pending insert, pero no se lee el código que escribe en el archivo para ver si hace rollback.
Abierto:
- tasks cancel
- agent cancel check in thread

### Trabajo 3
consulta: dónde se limpia el archivo o se deshace la escritura parcial cuando se cancela la generación
keep: M7 M12
leído: rollback pending, clear pending insert, cancel current, handle user input, cancel all, run insert async, cancel level1, handle route
Cerrado:
encontré el mecanismo de limpieza: al cancelar, cancel current invoca cancel all - cancel level1 setea agent cancel y el hilo de run insert async detecta esa bandera y llama a rollback pending que restaura el contenido previo p before de los archivos modificados en st pending y los escribe con write text file
Abierto:
- agent cancel check in run insert async thread body


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, cancel level1, clear pending insert, cancel all
    extra: Ai Controller, handle level2 harness

T3
    visto: rollback pending, clear pending insert, cancel current, handle user input
    extra: write text file, load state

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  rollback pending → apply tool → ai trace escape
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T2 → Ai Controller
  T2 → handle level2 harness
  T3 → write text file
  T3 → load state
