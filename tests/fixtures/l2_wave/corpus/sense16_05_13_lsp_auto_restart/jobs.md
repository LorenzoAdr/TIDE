### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M1 M4
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, reader loop, wait response, restart lsp for workspace, start, on lsp notification
Cerrado:
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, reader loop, wait response, restart lsp for workspace, start, on lsp notification
Abierto:
- send request
- ensure backend started

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo o timeout
keep: M5 M4
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
encontré el objeto de la consulta: el reinicio se dispara en lsp missing install tras fallo de instalación y se programa con debounce en schedule debounced lsp restart tras cambios de entorno; el efecto es restart lsp for workspace que reinicia el proveedor de símbolos y reabre documentos
Abierto:
- apply workspace settings
- setup build environment watching

### Trabajo 3
consulta: dónde se detecta que el proceso del servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M1 M2 M9
leído: reader loop, wait response, on lsp notification, start, send request, schedule debounced lsp restart, set eof handler, handle transport error
Cerrado:
leído: reader loop, wait response, on lsp notification, start, send request, schedule debounced lsp restart, set eof handler, handle transport error
Abierto:
- send lsp request


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, reader loop
    extra: async worker main, completions at

T2
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T3
    visto: reader loop, wait response, on lsp notification, start
    extra: open host pty, contains

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → async worker main
  T1 → completions at
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → open host pty
  T3 → contains
