### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, on lsp missing install
Abierto:
- run custom event drain

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o dejado de responder para invocar el reinicio diferido
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, process build environment updates, clangd process alive, process alive, run custom event drain, run
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, process build environment updates, clangd process alive, process alive, run custom event drain, run
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, process build environment updates
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
