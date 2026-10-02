### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder para disparar el reinicio
keep: M1 M11
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: qué evento o fallo detecta que el servidor LSP ha caído y llama a programar el reinicio
keep: M6
leído: schedule debounced lsp restart, on transport reader eof
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que detecta la caída y programa el reinicio; lo leído transport reader eof detecta el EOF y limpia el PID, pero no invoca a schedule debounced lsp restart ni a ningún otro programador de reinicio visible en su cuerpo ni en sus callers callees leídos
Abierto:
- on transport reader eof
- stop (llamada a reinicio)
- on lsp notification

### Trabajo 3
consulta: quién invoca a schedule debounced lsp restart cuando el servidor LSP muere o se desconecta
keep: M1
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching
Cerrado:
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T2
    visto: schedule debounced lsp restart, on transport reader eof
    extra: apply workspace settings, setup build environment watching

T3
    visto: schedule debounced lsp restart, apply workspace settings, setup build environment watching
    extra: Workspace Config, restart lsp for workspace

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → Workspace Config
  T2 → Workspace Config
  T3 → Workspace Config
  T3 → restart lsp for workspace
