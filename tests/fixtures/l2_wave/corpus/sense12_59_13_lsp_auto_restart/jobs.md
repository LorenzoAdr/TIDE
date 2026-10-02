### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7
leído: wait until healthy, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección automática de caída del servidor de lenguaje LSP en tiempo de ejecución; lo leído son reinicios programáticos tras cambios de configuración o instalación faltante, y un sistema de health-check HTTP para el backend de IA llama, no para el LSP
Abierto:
- start server
- on server exit
- on lsp crash

### Trabajo 2
consulta: dónde se inicia el proceso del servidor de lenguaje LSP y cómo se gestiona su ciclo de vida
keep: M2
leído: start, initialize, spawn language server, stop, on transport reader eof
Cerrado:
encontré el mecanismo de inicio y ciclo de vida del servidor LSP: el arranque se inicia en start que llama a spawn language server para hacer fork exec y luego a initialize para el handshake LSP, y el ciclo de vida se gestiona mediante stop que envía SIGTERM SIGKILL y limpia estado y transport reader eof que detecta la caída del proceso hijo .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje cuando se detecta que el proceso hijo ha terminado
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se reinicia el servidor de lenguaje cuando se detecta que el proceso hijo ha terminado
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, health ok, restart lsp for workspace, schedule debounced lsp restart
    extra: ensure ready, start server

T2
    visto: start, initialize, spawn language server, stop
    extra: set notification handler, set reader eof handler

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → set notification handler
  T2 → set reader eof handler
