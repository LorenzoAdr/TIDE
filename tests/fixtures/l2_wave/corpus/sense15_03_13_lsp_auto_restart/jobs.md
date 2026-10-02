### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder para disparar un reinicio
keep: M2 M5
leído: reader loop, wait response, start, schedule debounced lsp restart, apply workspace settings, restart lsp for workspace, run
Cerrado:
leído: reader loop, wait response, start, schedule debounced lsp restart, apply workspace settings, restart lsp for workspace, run
Abierto:
- send request
- ensure backend started

### Trabajo 2
consulta: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder para disparar un reinicio
keep: M1 M4
leído: on transport reader eof, start, set reader eof handler, reader loop, restart lsp for workspace
Cerrado:
encontré el mecanismo de detección: el reader loop del transport detecta la caída EOF error de lectura e invoca el handler registrado, que es on transport reader eof Sin embargo, este handler solo limpia estado ready false, waitpid y NO dispara el reinicio directamente. El reinicio restart lsp for workspace es invocado por otros caminos run, on lsp missing install, no desde el detector de caída. Falta el enlace causal la detección de caída y el disparo del reinicio.
Abierto:
- on transport reader eof restart lsp for workspace

### Trabajo 3
consulta: dónde se conecta el handler de caída del transport al reinicio del servidor LSP
keep: M1 M3
leído: on transport reader eof, start, set reader eof handler, reader loop
Cerrado:
encontré el objeto de la consulta: la conexión es directa en start, que registra el handler de caída on transport reader eof en el transport mediante set reader eof handler; el reinicio del servidor LSP no se dispara desde este handler solo limpia estado, por lo que la conexión al reinicio no existe en este flujo.
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: reader loop, wait response, start, schedule debounced lsp restart
    extra: open host pty, contains

T2
    visto: on transport reader eof, start, set reader eof handler, reader loop
    extra: set notification handler, set response acceptance filter

T3
    visto: on transport reader eof, start, set reader eof handler, reader loop
    extra: set notification handler, set response acceptance filter

entre abiertas:
  T1=>T2  mismo objeto: reader loop
  T1=>T3  mismo objeto: reader loop
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → open host pty
  T1 → contains
  T2 → set notification handler
  T2 → set response acceptance filter
  T3 → set notification handler
  T3 → set response acceptance filter
