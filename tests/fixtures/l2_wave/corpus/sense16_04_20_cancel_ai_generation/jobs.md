### Trabajo 1
consulta: qué hace cancel level1 para limpiar el archivo a medias
keep: M1 M7
leído: cancel level1, cancel all, Ai Controller
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza del archivo en cancel level1; lo leído muestra que cancel level1 solo setea un flag agent cancel y loguea, sin tocar archivos ni buffers de escritura
Abierto:
- agent cancel handler
- agent thread loop
- file write logic

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la escritura de la IA
keep: M2 M6
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se trunca o borra el contenido del archivo cuando se cancela la inserción pendiente
keep: M1 M10
leído: truncate list impl, truncate utf8, clear pending insert, cancel current
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de truncamiento o borrado del archivo al cancelar la inserción pendiente; lo leído muestra que cancel current solo invoca a clear pending insert que resetea flags y despierta el ciclo y que truncate utf8 opera sobre strings en memoria, sin evidencia de escritura o truncamiento de archivos en disco en este flujo
Abierto:
- handle route
- handle user input
- cancel all


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, cancel all, Ai Controller
    extra: join agent thread, join task thread

T2
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: truncate list impl, truncate utf8, clear pending insert, cancel current
    extra: is symptom edge call, seed overlap

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T3  extra toca visto: cancel all
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → join agent thread
  T1 → join task thread
  T2 → handle user input
  T2 → handle route
  T3 → is symptom edge call
  T3 → seed overlap
