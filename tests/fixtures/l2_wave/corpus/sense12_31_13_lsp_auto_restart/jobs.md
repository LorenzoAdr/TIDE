### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: notify lsp status, set lsp status callback, restart lsp for workspace, schedule debounced lsp restart, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, set lsp status callback, restart lsp for workspace, schedule debounced lsp restart, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 2
consulta: dónde se detiene el servidor de lenguaje cuando se para
keep: M2 M5
leído: notify lsp status, set lsp status callback
Cerrado:
leído: notify lsp status, set lsp status callback
Abierto:
- finish python lsp start locked

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje tras un fallo o pérdida de respuesta
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, notify lsp status, set lsp status callback
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio tras fallo o pérdida de respuesta; lo leído cubre reinicio tras instalación lsp missing install y tras cambio de entorno schedule debounced lsp restart pero no hay código que detecte timeout crash y dispare el reinicio
Abierto:
- lsp health check
- lsp process monitor
- notify lsp status caller crash


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: notify lsp status, set lsp status callback, restart lsp for workspace, schedule debounced lsp restart
    extra: Make Editor Panel, on document opened

T2
    visto: notify lsp status, set lsp status callback
    extra: Make Editor Panel, finish python lsp start locked

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: notify lsp status
  T1=>T3  mismo objeto: notify lsp status
  T2=>T3  mismo objeto: notify lsp status
hacia el resto:
  T1 → Make Editor Panel
  T1 → on document opened
  T2 → Make Editor Panel
  T2 → on document opened
  T3 → on workspace opened
  T3 → set workspace clangd options
