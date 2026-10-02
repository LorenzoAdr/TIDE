### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras detectar que el proceso ha muerto
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → on workspace opened
