### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje falta o no responde y se dispara el reinicio
keep: M11 M12
leído: start completion server, escalate, ensure completion server, health ok
Cerrado:
leído: start completion server, escalate, ensure completion server, health ok
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M6
leído: on lsp notification, initialize, restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, on lsp missing install, set notification handler, spawn language server
Cerrado:
leído: on lsp notification, initialize, restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, on lsp missing install, set notification handler, spawn language server
Abierto:
- send lsp request


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: start completion server, escalate, ensure completion server, health ok
    extra: stop listener on port, stop owned unlocked

T3
    visto: on lsp notification, initialize, restart lsp for workspace, schedule debounced lsp restart
    extra: contains, is lsp trackable path
    entre interno: restart lsp for workspace → initialize

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  sin camino
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → stop listener on port
  T2 → stop owned unlocked
  T3 → contains
  T3 → is lsp trackable path
