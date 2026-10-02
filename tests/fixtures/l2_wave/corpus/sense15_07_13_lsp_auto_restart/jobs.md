### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M8
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M6
leído: spawn language server, start, on transport reader eof
Cerrado:
leído: spawn language server, start, on transport reader eof
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP deja de responder y se dispara el reinicio
keep: M3 M6
leído: on transport reader eof, spawn language server, schedule debounced lsp restart, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de timeout falta de respuesta; lo leído solo cubre la detección por caída del proceso EOF y el disparo de reinicio por configuración, pero no hay evidencia de un watchdog de inactividad.
Abierto:
- reader loop
- on lsp notification
- process build environment updates


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: spawn language server, start, on transport reader eof
    extra: c str, set notification handler

T3
    visto: on transport reader eof, spawn language server, schedule debounced lsp restart, start
    extra: c str, apply workspace settings

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: spawn language server
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → c str
  T2 → set notification handler
  T3 → c str
  T3 → apply workspace settings
