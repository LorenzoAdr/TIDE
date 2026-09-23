### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started
Abierto:
- set backend epoch
- register backend wake callback

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: notify lsp status, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 4
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Abierto:
- ensure backend started


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: run level1 async, handle route

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: notify lsp status, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T4
    visto: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T1=>T4  mismo objeto: restart lsp for workspace
  T2=>T3  sin camino
  T2=>T4  mismo objeto: restart lsp for workspace
  T3=>T4  sin camino
hacia el resto:
  T1 → run level1 async
  T1 → handle route
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → Make Editor Panel
  T3 → on document opened
