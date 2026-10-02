### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder
keep: M5
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje tras detectar que se ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, set lsp status callback, Application, run, on lsp missing install, process build environment updates
Cerrado:
encontré el mecanismo de reinicio tras caída: el disparo es schedule debounced lsp restart activado por cambios de entorno o instalación de LSP que marca pending lsp restart true el efecto se ejecuta en process build environment updates llamado desde el loop de eventos run custom event drain que verifica el debounce y llama a restart lsp for workspace .
Abierto:
- finish python lsp start locked
- pending lsp restart
- run custom event drain

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M5
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 4
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M5
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- stop lsp


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, set lsp status callback
    extra: on workspace opened, set workspace clangd options

T3
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T4
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

entre abiertas:
  T1=>T2  mismo objeto: notify lsp status
  T1=>T3  mismo objeto: notify lsp status
  T1=>T4  mismo objeto: notify lsp status
  T2=>T3  mismo objeto: notify lsp status
  T2=>T4  mismo objeto: restart lsp for workspace
  T3=>T4  mismo objeto: notify lsp status
hacia el resto:
  T1 → Make Editor Panel
  T1 → on document opened
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → Make Editor Panel
  T3 → on document opened
