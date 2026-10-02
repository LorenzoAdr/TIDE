### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M3 M6
leído: spawn language server, did close, stop, start, on transport reader eof, set reader eof handler, reader loop
Cerrado:
leído: spawn language server, did close, stop, start, on transport reader eof, set reader eof handler, reader loop
Abierto:
- send lsp notification

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M2 M5
leído: on transport reader eof, stop, spawn language server, start, restart lsp for workspace, schedule debounced lsp restart, run
Cerrado:
encontré el mecanismo de reinicio tras caída: el cliente LSP detecta la caída en transport reader eof EOF del transporte, pero NO reinicia automáticamente; delega la detección al bucle principal run que monitorea el estado ready del cliente y, si es falso, invoca schedule debounced lsp restart para programar un reinicio diferido que ejecuta restart lsp for workspace
Abierto:
- run (sección de polling de LSP, truncada en el peek)

### Trabajo 3
consulta: dónde el bucle principal monitorea el estado del cliente LSP y decide reiniciar si no está listo
keep: M2 M9
leído: start, on transport reader eof, restart lsp for workspace, schedule debounced lsp restart, run
Cerrado:
leído: start, on transport reader eof, restart lsp for workspace, schedule debounced lsp restart, run
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: spawn language server, did close, stop, start
    extra: c str, normalize lsp path

T2
    visto: on transport reader eof, stop, spawn language server, start
    extra: request start, Workspace Indexer

T3
    visto: start, on transport reader eof, restart lsp for workspace, schedule debounced lsp restart
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  mismo objeto: spawn language server
  T1=>T3  mismo objeto: start
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → c str
  T1 → normalize lsp path
  T2 → request start
  T2 → Workspace Indexer
  T3 → set notification handler
  T3 → set response acceptance filter
