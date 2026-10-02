### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M3 M8
leído: restart lsp for workspace, wait response, send lsp request
Cerrado:
leído: restart lsp for workspace, wait response, send lsp request
Abierto:
- send request

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor LSP tras un fallo
keep: M1
leído: restart lsp for workspace, on lsp missing install
Cerrado:
leído: restart lsp for workspace, on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o no responde para programar el reinicio
keep: M1
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, wait response, send lsp request
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → send lsp request

T2
    visto: restart lsp for workspace, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T3
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → apply workspace settings
  T3 → setup build environment watching
