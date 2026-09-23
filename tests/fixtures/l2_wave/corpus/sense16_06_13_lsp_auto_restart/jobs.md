### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar el reinicio
keep: M3 M4
leído: on transport reader eof, start, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída que dispara el reinicio; lo leído muestra que transport reader eof solo marca estado ready false y restart lsp for workspace es llamado por run inicio y lsp missing install instalación faltante, pero no hay un camino desde el detector de caída al reinicio.
Abierto:
- on transport reader eof restart lsp for workspace

### Trabajo 2
consulta: dónde se reinicia el servidor LSP automáticamente tras detectar que se ha caído
keep: M5 M7
leído: on transport reader eof, stop, restart lsp for workspace, on lsp missing install, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección automática de caída que dispara el reinicio; lo leído muestra que on transport reader eof solo marca ready false y child pid -1 sin llamar a start, y que restart lsp for workspace es invocado únicamente por run inicio y on lsp missing install instalación de paquete, no por un detector de crash
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, restart lsp for workspace
    extra: set notification handler, set reader eof handler

T2
    visto: on transport reader eof, stop, restart lsp for workspace, on lsp missing install
    extra: Workspace Indexer, request start

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → Workspace Indexer
  T2 → request start
