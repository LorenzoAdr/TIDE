### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
encontré el mecanismo de reinicio del LSP: se dispara por cambio de entorno process build environment updates o instalación faltante on lsp missing install, y el efecto es restart lsp for workspace que reinicia el proveedor de símbolos y reabre documentos
Abierto:
- run custom event drain
- apply workspace settings
- setup build environment watching

### Trabajo 2
consulta: dónde detecta el proveedor de símbolos LSP que el servidor se ha caído o no responde
keep: M2
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 3
consulta: dónde detecta la aplicación que el proceso del servidor LSP se ha caído o no responde
keep: M2 M5
leído: notify lsp status, set lsp status callback, restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, ensure python lsp async, finish python lsp start locked, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída o no-respuesta del proceso LSP; lo leído es la lógica de arranque, notificación de estado y reinicio tras instalación, pero no el monitor de salud heartbeat timeout del proceso en ejecución
Abierto:
- on exit
- process exited

### Trabajo 4
consulta: dónde se detecta que el proceso del servidor LSP ha terminado inesperadamente o ha muerto
keep: M2 M4
leído: ensure python lsp async, finish python lsp start locked
Cerrado:
leído: ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

T2
    visto: notify lsp status, set lsp status callback, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T3
    visto: notify lsp status, set lsp status callback, restart lsp for workspace, schedule debounced lsp restart
    extra: Make Editor Panel, on document opened

T4
    visto: ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  sin camino
  T2=>T3  mismo objeto: notify lsp status
  T2=>T4  mismo objeto: ensure python lsp async
  T3=>T4  mismo objeto: ensure python lsp async
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → Make Editor Panel
  T2 → on document opened
  T3 → Make Editor Panel
  T3 → on document opened
