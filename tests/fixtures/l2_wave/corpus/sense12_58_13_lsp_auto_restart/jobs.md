### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M3
leído: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, start, stop
Cerrado:
encontré el mecanismo de detección y disparo: la caída se detecta en transport reader eof disparado por el transport al cerrar el pipe EOF, pero este callback SOLO limpia estado ready false waitpid . NO dispara el reinicio directamente. El reinicio se dispara desde restart lsp for workspace que es llamado por lsp missing install o run inicio, o programado vía schedule debounced lsp restart ante cambios de config. No hay un camino directo de transport reader eof a restart lsp for workspace en el código leído; la detección de caída parece ser pasiva limpieza y el reinicio activo está desacoplado o depende de un ciclo de vida superior no completamente rastreado en esta ola posiblemente application
Abierto:
- on lsp missing install
- on workspace opened

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor LSP tras un fallo de transporte
keep: M1 M4
leído: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, start
Cerrado:
encontré el objeto de la consulta: el reinicio diferido se programa en schedule debounced lsp restart que establece el deadline y se dispara tras el fallo de transporte detectado en transport reader eof que limpia el estado del proceso hijo, aunque el circuito causal completo el EOF y la programación del reinicio queda parcialmente abierto el EOF no llama directamente al scheduler en el código leído, sugiriendo un paso intermedio o una lógica de reconexión no leída en start o stop .
Abierto:
- run loop check

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart tras detectar el fallo en transport reader eof
keep: M8 M10
leído: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, start
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, start
    extra: apply workspace settings, setup build environment watching

T3
    visto: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
    extra: set notification handler, set response acceptance filter

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → set notification handler
  T3 → set response acceptance filter
