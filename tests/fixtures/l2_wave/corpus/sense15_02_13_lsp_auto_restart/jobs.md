### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7 M5
leído: wait until healthy, status text, health ok, ready, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: wait until healthy, status text, health ok, ready, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- start server

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje automáticamente tras detectar que se ha caído o no responde
keep: M2 M8
leído: restart lsp for workspace, schedule debounced lsp restart, complete, ready, start completion server, stop owned unlocked, ensure completion server
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, complete, ready, start completion server, stop owned unlocked, ensure completion server
Abierto:
- start server
- stop listener on port

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar el reinicio
keep: M2 M3
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, status text, health ok, ready
    extra: ensure ready, start server

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, complete, ready
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → stop owned unlocked

T3
    visto: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  mismo objeto: ready
  T1=>T2  start completion server → wait until healthy
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → set notification handler
  T3 → set reader eof handler
