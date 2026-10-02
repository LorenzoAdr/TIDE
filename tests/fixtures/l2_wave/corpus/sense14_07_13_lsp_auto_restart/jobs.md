### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: start completion server, status text, health ok, ensure completion server
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída y reinicio automático; lo leído muestra que health ok detecta la caída HTTP GET health y start completion server la usa para abortar el inicio, pero no hay un bucle de monitorización ni un disparo de reinicio en el código leído ensure completion server solo inicia una vez
Abierto:
- start server
- restart server
- monitor loop

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M5
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart para reiniciar el servidor
keep: M1 M2
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching, restart lsp for workspace, run, on lsp missing install, guard
Cerrado:
encontré el objeto de la consulta: schedule debounced lsp restart se llama desde setup build environment watching callbacks de cambio de entorno artefactos y se ejecuta en el tick de la UI guard cuando vence el deadline
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: start completion server, status text, health ok, ensure completion server
    extra: stop listener on port, stop owned unlocked

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
    extra: on lsp missing install, on workspace opened

T3
    visto: schedule debounced lsp restart, apply workspace settings, setup build environment watching, restart lsp for workspace
    extra: Workspace Config, apply clangd workspace config

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → stop listener on port
  T1 → stop owned unlocked
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → Workspace Config
  T3 → apply clangd workspace config
