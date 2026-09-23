### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2
leído: on transport reader eof, start, set reader eof handler, set notification handler, set response acceptance filter, reader loop
Cerrado:
encontré el mecanismo de detección de caída: el servidor se considera caído cuando el transporte detecta EOF o error de lectura en el pipe reader loop, lo que invoca el handler on transport reader eof en lsp client, que limpia el estado ready false y espera al proceso hijo con waitpid. No hay detección de dejado de responder timeout hang en el código leído; solo detección de muerte del proceso por cierre de canal.
Abierto:
- read message

### Trabajo 2
consulta: dónde se dispara el reinicio automático del servidor LSP tras detectar que el proceso se ha caído
keep: M4 M12
leído: on transport reader eof, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático; lo leído muestra que transport reader eof solo limpia el estado ready false waitpid y start es el constructor de la conexión, pero no hay código que invoque a start tras la caída en el cuerpo de transport reader eof ni en sus llamadores inmediatos visibles
Abierto:
- on transport reader eof caller chain
- spawn language server

### Trabajo 3
consulta: dónde se programa el reinicio diferido del servidor LSP tras un fallo
keep: M1
leído: restart lsp for workspace, on lsp missing install
Cerrado:
leído: restart lsp for workspace, on lsp missing install
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, set reader eof handler, set notification handler
    extra: open host pty, contains

T2
    visto: on transport reader eof, start
    extra: set notification handler, set reader eof handler

T3
    visto: restart lsp for workspace, on lsp missing install
    extra: run level1 async, handle route

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → open host pty
  T1 → contains
  T2 → spawn language server
  T3 → run level1 async
  T3 → handle route
