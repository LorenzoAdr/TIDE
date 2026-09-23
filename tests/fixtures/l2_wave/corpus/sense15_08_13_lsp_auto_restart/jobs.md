### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M5
leído: on lsp notification, spawn language server
Cerrado:
leído: on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio del servidor LSP cuando se detecta un fallo
keep: M1 M4
leído: restart lsp for workspace, on lsp missing install
Cerrado:
leído: restart lsp for workspace, on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder y se dispara el reinicio
keep: M1 M12
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, restart lsp for workspace, notify lsp status, set lsp status callback
Cerrado:
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, restart lsp for workspace, notify lsp status, set lsp status callback
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp notification, spawn language server
    extra: contains, is lsp trackable path

T2
    visto: restart lsp for workspace, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T3
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification
    extra: async worker main, completions at

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: on lsp notification
  T1=>T3  spawn language server → initialize → did open → generation → parse wait timeout ms
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → contains
  T1 → is lsp trackable path
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → async worker main
  T3 → completions at
