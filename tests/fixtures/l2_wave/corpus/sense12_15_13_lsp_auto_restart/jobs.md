### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status, ensure python lsp async
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status, ensure python lsp async
Abierto:
- finish python lsp start locked

### Trabajo 2
consulta: quién dispara el reinicio del servidor LSP cuando se detecta que el proceso ha muerto
keep: M5
leído: clangd process alive, notify lsp status
Cerrado:
leído: clangd process alive, notify lsp status
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia el proceso del servidor LSP tras ser programado el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
encontré el mecanismo de inicio tras el reinicio programado: schedule debounced lsp restart programa el flag y deadline; process build environment updates llamada desde run custom event drain verifica el flag y el tiempo, y si están listos invoca restart lsp for workspace
Abierto:
- run custom event drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
    extra: on lsp missing install, on workspace opened

T2
    visto: clangd process alive, notify lsp status, restart lsp for workspace, schedule debounced lsp restart
    extra: read file text, write terminal init script

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → read file text
  T2 → write terminal init script
  T3 → on lsp missing install
  T3 → on workspace opened
