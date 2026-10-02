### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
keep: M1 M10
leído: maybe show lsp missing toast, Make Lsp Missing Toast Overlay, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: maybe show lsp missing toast, Make Lsp Missing Toast Overlay, restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio automático
keep: M2 M7
leído: schedule debounced lsp restart, restart lsp for workspace, set background paused
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, set background paused
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: maybe show lsp missing toast, Make Lsp Missing Toast Overlay, restart lsp for workspace, schedule debounced lsp restart
    extra: Application, find tuide source root

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, set background paused
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → Application
  T1 → find tuide source root
  T2 → apply workspace settings
  T2 → setup build environment watching
