### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M4 M6
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M11
leído: on lsp notification, spawn language server
Cerrado:
leído: on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder para disparar el reinicio
keep: M8 M11
leído: schedule debounced lsp restart, restart lsp for workspace, apply workspace settings, setup build environment watching
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, apply workspace settings, setup build environment watching
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T2
    visto: on lsp notification, spawn language server
    extra: contains, is lsp trackable path

T3
    visto: schedule debounced lsp restart, restart lsp for workspace, apply workspace settings, setup build environment watching
    extra: Workspace Config, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  sin camino
hacia el resto:
  T1 → Workspace Config
  T2 → contains
  T2 → is lsp trackable path
  T3 → Workspace Config
  T3 → on lsp missing install
