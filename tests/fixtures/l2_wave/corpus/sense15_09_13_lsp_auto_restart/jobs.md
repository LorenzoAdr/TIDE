### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M11
leído: on lsp notification, spawn language server
Cerrado:
leído: on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP automáticamente tras una caída
keep: M10 M12
leído: restart lsp for workspace
Cerrado:
leído: restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se vincula la detección de caída del LSP con el reinicio automático
keep: M1 M5
leído: schedule debounced lsp restart, on connection complete, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, on connection complete, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp notification, spawn language server
    extra: contains, is lsp trackable path

T2
    visto: restart lsp for workspace
    extra: on lsp missing install, on workspace opened

T3
    visto: schedule debounced lsp restart, on connection complete, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → contains
  T1 → is lsp trackable path
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → apply workspace settings
  T3 → setup build environment watching
