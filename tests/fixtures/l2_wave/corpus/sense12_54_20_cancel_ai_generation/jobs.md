### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: cancel level1, ai trace escape, clear hover if
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar IA; lo leído es un escaper de strings para logs ai trace escape y un limpiador de hover UI clear hover if, sin relación con el flujo de cancelación
Abierto:
- handle problems scrollbar mouse
- key handler
- event loop
- cancel all

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA
keep: M1
leído: cancel current, clear pending insert, cancel all, busy, cancel level1, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré limpieza de archivo ni deshacer de inserción parcial al cancelar la IA; lo leído es un reset de estado en memoria pending insert false, insert anchor en clear pending insert y cancel current, sin escritura ni rollback en el sistema de archivos
Abierto:
- handle user input
- handle route
- apply insert


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, ai trace escape, clear hover if
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T1 → handle problems scrollbar mouse
  T2 → handle user input
  T2 → handle route
