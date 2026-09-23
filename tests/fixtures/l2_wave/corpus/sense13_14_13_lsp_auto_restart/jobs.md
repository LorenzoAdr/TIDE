### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: status text, ready, start completion server, ensure completion server, health ok
Cerrado:
leído: status text, ready, start completion server, ensure completion server, health ok
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje cuando se detecta una caída
keep: M1 M5
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o deja de responder y se dispara el reinicio
keep: M2 M4
leído: on transport reader eof, schedule debounced lsp restart, completion wait timeout ms, parse wait timeout ms, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta health check timeout que dispare el reinicio; lo leído son timeouts de espera de respuesta para completions hover completion wait timeout ms, parse wait timeout ms y la función de reinicio en sí restart lsp for workspace, pero no el código que decide que el LSP se ha caído y llama a schedule debounced lsp restart o restart lsp for workspace por esa causa. on transport reader eof solo maneja EOF, no timeouts.
Abierto:
- on transport reader eof
- check health
- ping timeout
- run (bucle de detección de caída)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: status text, ready, start completion server, ensure completion server
    extra: cache dir, cli runnable

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: setup build environment watching, apply workspace settings

T3
    visto: on transport reader eof, schedule debounced lsp restart, completion wait timeout ms, parse wait timeout ms
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → cache dir
  T1 → cli runnable
  T2 → setup build environment watching
  T2 → apply workspace settings
  T3 → apply workspace settings
  T3 → setup build environment watching
