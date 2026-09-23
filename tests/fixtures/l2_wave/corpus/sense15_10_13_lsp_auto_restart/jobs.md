### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M8
leído: restart lsp for workspace, schedule debounced lsp restart, process alive, spawn clangd, spawn language server
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, process alive, spawn clangd, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el proceso del servidor de lenguaje ha muerto o no responde y se dispara el reinicio
keep: M1 M2
leído: wait until healthy, ensure completion server, health ok, start completion server
Cerrado:
leído: wait until healthy, ensure completion server, health ok, start completion server
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M5
leído: on lsp notification, initialize
Cerrado:
leído: on lsp notification, initialize
Abierto:
- send lsp request


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, process alive, spawn clangd
    extra: on lsp missing install, on workspace opened

T2
    visto: wait until healthy, ensure completion server, health ok, start completion server
    extra: ensure ready, start server

T3
    visto: on lsp notification, initialize
    extra: contains, is lsp trackable path

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → ensure ready
  T2 → start server
  T3 → contains
  T3 → is lsp trackable path
