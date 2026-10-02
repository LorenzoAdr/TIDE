### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída y reinicio automático del servidor de lenguaje; lo leído es el arranque inicial start completion server y el reinicio manual del LSP de código restart lsp for workspace, pero falta el watchdog o loop que detecte la caída y dispare el reinicio
Abierto:
- start server
- watchdog loop
- health check timer
- on server crash

### Trabajo 2
consulta: dónde se gestiona el ciclo de vida del servidor de lenguaje para detectar si se cae o no responde
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se gestiona el ciclo de vida del servidor de lenguaje para detectar si se cae o no responde
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart cuando el servidor deja de responder
keep: M1 M10
leído: schedule debounced lsp restart, wait response
Cerrado:
leído: schedule debounced lsp restart, wait response
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: status text, start completion server, ensure completion server, health ok
    extra: cache dir, stop listener on port

T2
    visto: (nada)

T3
    visto: schedule debounced lsp restart, wait response, send request
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  sin camino
hacia el resto:
  T1 → cache dir
  T1 → stop listener on port
  T3 → apply workspace settings
  T3 → setup build environment watching
