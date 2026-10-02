### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M4 M6
leído: schedule debounced lsp restart, on transport reader eof, apply workspace settings, setup build environment watching, start, restart lsp for workspace, run, on lsp missing install
Cerrado:
encontré el mecanismo de reinicio del LSP: la caída se detecta en transport reader eof EOF del transporte, pero el reinicio propiamente dicho se orquesta en Application mediante restart lsp for workspace . Este reinicio se dispara por cambio de entorno apply workspace settings - restart lsp for workspace por cambio de artefactos de build setup build environment watching - schedule debounced lsp restart - restart lsp for workspace en el loop principal, aunque el cuerpo del loop no se leyó, el patrón es claro, o tras instalación de LSP lsp missing install - restart lsp for workspace . El cuerpo de restart lsp for workspace confirma que reinicia el proveedor de símbolos y reabre documentos.
Abierto:
- run loop body

### Trabajo 2
consulta: dónde se detecta que el servidor LSP deja de responder o se congela y se dispara el reinicio
keep: M3 M11
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on transport reader eof, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de congelación timeout global watchdog que dispare el reinicio; lo leído son timeouts por request específica completion parse y un handler de desconexión física EOF, pero ninguno de ellos invoca al reinicio ni implementa un watchdog de actividad
Abierto:
- watchdog loop
- on response timeout
- check server health


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, on transport reader eof, apply workspace settings, setup build environment watching
    extra: Workspace Config, apply clangd workspace config

T2
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on transport reader eof
    extra: async worker main, completions at

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T2  initialize → did open → generation → parse wait timeout ms
hacia el resto:
  T1 → Workspace Config
  T1 → apply clangd workspace config
  T2 → async worker main
  T2 → completions at
