### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a la función de reinicio del LSP cuando se detecta que el proceso no está vivo
keep: M5
leído: clangd process alive, process alive
Cerrado:
leído: clangd process alive, process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia el proceso de reinicio del LSP tras detectar que el servidor no responde
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: clangd process alive, process alive, schedule debounced lsp restart
    extra: read file text, english strings

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, process alive
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: clangd process alive
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → read file text
  T2 → english strings
  T3 → on lsp missing install
  T3 → on workspace opened
