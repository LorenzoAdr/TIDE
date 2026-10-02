### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M2
leído: ai trace escape, clear hover
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído son utilidades de string ai trace escape y estado de hover clear hover, no el handler de eventos
Abierto:
- handle console panel mouse

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA
keep: M1 M3
leído: cancel current, clear pending insert, handle user input, handle route, cancel all, is cancel input, cancel level1, run insert async
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA; lo leído muestra que clear pending insert solo borra el estado interno pending insert, insert anchor y cancel all solo envían señales de cancelación agent cancel, tasks cancel, pero no hay código visible que restaure el buffer del editor o deshaga texto insertado parcialmente
Abierto:
- undo partial insert
- restore buffer on cancel


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, clear hover
    extra: update ai result hover, handle console panel mouse

T2
    visto: cancel current, clear pending insert, handle user input, handle route
    extra: handle level2 harness, background

entre abiertas:
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → update ai result hover
  T1 → handle console panel mouse
  T2 → handle level2 harness
  T2 → background
