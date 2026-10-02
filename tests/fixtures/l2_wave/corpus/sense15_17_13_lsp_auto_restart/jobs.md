### Trabajo 1
consulta: cómo detecta la aplicación que el servidor de lenguaje LSP se ha caído o no responde y qué dispara el reinicio automático
keep: M1 M8
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, set background paused
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, set background paused
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y qué dispara el reinicio automático
keep: M3 M5
leído: restart lsp for workspace, on lsp notification, on lsp missing install
Cerrado:
leído: restart lsp for workspace, on lsp notification, on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje LSP tras un fallo o falta de respuesta
keep: M5 M6
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, set background paused
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, on lsp notification, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → on workspace opened
  T3 → set workspace clangd options
