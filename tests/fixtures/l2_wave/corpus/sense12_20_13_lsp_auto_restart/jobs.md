### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, lsp symbol provider cpp, symbol provider hpp
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, lsp symbol provider cpp, symbol provider hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el reinicio del servidor LSP tras detectar que falta o ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Cerrado:
encontré el inicio del reinicio tras detectar caída es on lsp missing install, que tras instalar el pack llama a restart lsp for workspace
Abierto:
- lsp missing toast state

### Trabajo 3
consulta: dónde se detecta que el servidor LSP ha dejado de responder timeout o crash y se dispara el reinicio automático
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, lsp symbol provider cpp, symbol provider hpp
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

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
