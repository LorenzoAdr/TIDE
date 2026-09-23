### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M1 M4
leído: on transport reader eof, restart lsp for workspace, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída que dispara el reinicio; lo leído muestra que transport reader eof solo limpia estado ready false waitpid sin llamar a restart lsp for workspace y los únicos callers leídos de restart lsp for workspace son run inicio y lsp missing install instalación faltante, no un detector de caída.
Abierto:
- on transport reader eof caller chain
- notify lsp status

### Trabajo 2
consulta: dónde se programa o ejecuta el reinicio diferido del servidor LSP cuando cambia el entorno o se detecta un fallo
keep: M4
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching, restart lsp for workspace, run
Cerrado:
encontré el mecanismo de programación y ejecución del reinicio diferido: schedule debounced lsp restart programa el deadline 2s y setup build environment watching lo dispara ante cambios de entorno; la ejecución se delega a restart lsp for workspace
Abierto:
- lsp restart deadline
- lsp restart deadline consumption site


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, restart lsp for workspace, start
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, apply workspace settings, setup build environment watching, restart lsp for workspace
    extra: Workspace Config, apply clangd workspace config

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → Workspace Config
  T2 → apply clangd workspace config
