### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: notify lsp status, join thread if joinable, ensure python lsp async, finish python lsp start locked, lsp symbol provider hpp, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: notify lsp status, join thread if joinable, ensure python lsp async, finish python lsp start locked, lsp symbol provider hpp, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- stop lsp

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, ensure python lsp async, finish python lsp start locked, on lsp missing install, run
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, ensure python lsp async, finish python lsp start locked, on lsp missing install, run
Abierto:
- stop lsp
- ensure backend started


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: notify lsp status, join thread if joinable, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, ensure python lsp async
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: notify lsp status
hacia el resto:
  T1 → Make Editor Panel
  T1 → on document opened
  T2 → on workspace opened
  T2 → set workspace clangd options
