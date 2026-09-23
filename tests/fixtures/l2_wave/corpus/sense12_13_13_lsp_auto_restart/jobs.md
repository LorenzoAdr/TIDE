### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
keep: M5
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de detección de fallo del servidor LSP unresponsive timeout que dispara el restart; lo leído son triggers por cambio de entorno apply workspace settings, setup build environment watching
Abierto:
- lsp server heartbeat
- lsp connection monitor
- process build environment updates

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, apply workspace settings, setup build environment watching
    extra: Workspace Config, restart lsp for workspace

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T2  apply workspace settings → restart lsp for workspace
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → Workspace Config
  T2 → apply clangd workspace config
