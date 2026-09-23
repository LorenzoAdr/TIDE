### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se programa su reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del LSP; lo leído es la programación del reinicio tras cambios de entorno schedule debounced lsp restart y la ejecución del reinicio restart lsp for workspace, pero no hay lógica de ping timeout en el bucle principal run ni en los callers leídos
Abierto:
- lsp heartbeat
- lsp ping
- lsp timeout
- check lsp alive

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se programa su reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se programa su reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se comprueba si el servidor de lenguaje está vivo o responde antes de enviarle una petición
keep: M5
leído: process alive, clangd process alive, send request
Cerrado:
leído: process alive, clangd process alive, send request
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se llama al reinicio del LSP cuando el proceso no está vivo
keep: M5
leído: process alive, clangd process alive
Cerrado:
leído: process alive, clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, restart lsp for workspace, run
    extra: setup build environment watching, apply workspace settings

T2
    visto: (nada)

T3
    visto: process alive, clangd process alive, send request
    extra: read file text, TUIDE MON

T4
    visto: process alive, clangd process alive
    extra: read file text

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  mismo objeto: process alive
hacia el resto:
  T1 → setup build environment watching
  T1 → apply workspace settings
  T3 → read file text
  T3 → TUIDE MON
  T4 → read file text
