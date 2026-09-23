### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder
keep: M5
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar su caída
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, set lsp status callback, Application, run, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, set lsp status callback, Application, run, on lsp missing install
Abierto:
- finish python lsp start locked
- ensure backend started

### Trabajo 3
consulta: dónde se llama a reiniciar el servidor de lenguaje tras detectar su caída
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, notify lsp status, set lsp status callback, Application
Cerrado:
encontré el objeto de la consulta: el reinicio tras caída se dispara en lsp missing install tras instalación exitosa y en run vía ensure backend started ambos llaman a restart lsp for workspace el callback de detección notify lsp status se registra en el constructor Application pero su cuerpo no fue leído para confirmar la lógica de caída vs instalación faltante
Abierto:
- finish python lsp start locked
- ensure backend started
- Application constructor body (callback registration)
- notify lsp status caller logic for crash detection

### Trabajo 4
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder
keep: M2 M5
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked, restart lsp for workspace, schedule debounced lsp restart, stop lsp
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked, restart lsp for workspace, schedule debounced lsp restart, stop lsp
Abierto:
- signal async worker stop locked
- stop lsp locked finalize


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, notify lsp status, set lsp status callback
    extra: on workspace opened, set workspace clangd options

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T4
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened
    entre interno: ensure python lsp async → stop lsp

entre abiertas:
  T1=>T2  mismo objeto: notify lsp status
  T1=>T3  mismo objeto: notify lsp status
  T1=>T4  mismo objeto: notify lsp status
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  mismo objeto: restart lsp for workspace
  T3=>T4  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → Make Editor Panel
  T1 → on document opened
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → on workspace opened
  T3 → set workspace clangd options
