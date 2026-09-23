### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7
leído: wait until healthy, health ok, start completion server, ensure completion server
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída para disparar reinicio; lo leído health ok, wait until healthy, ensure completion server solo detecta estado en arranque o reutilización, pero no hay loop de monitorización ni llamada a reinicio tras fallo en tiempo de ejecución
Abierto:
- start server
- restart on failure
- monitor loop
- health check timer

### Trabajo 2
consulta: dónde se programa o dispara el reinicio diferido del servidor LSP tras un fallo
keep: M4
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart tras un fallo o caída del servidor
keep: M1
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, health ok, start completion server, ensure completion server
    extra: ensure ready, start server

T2
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T3
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → apply workspace settings
  T3 → setup build environment watching
