### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: restart lsp for workspace, schedule debounced lsp restart, health ok
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, health ok
Abierto:
- start server

### Trabajo 2
consulta: dónde se inicia el servidor de lenguaje y se establece la conexión inicial
keep: M6 M11
leído: start server
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el arranque del servidor de lenguaje; lo leído es el arranque del servidor de embeddings llama-server en embedding backend, no del LSP
Abierto:
- run level1 async
- make texlab spec
- cancel live lsp on cursor move

### Trabajo 3
consulta: dónde se verifica la salud del servidor de lenguaje y se programa el reinicio si falla
keep: M1 M11
leído: restart lsp for workspace, notify lsp status, set lsp status callback
Cerrado:
leído: restart lsp for workspace, notify lsp status, set lsp status callback
Abierto:
- finish python lsp start locked


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, health ok
    extra: on lsp missing install, on workspace opened

T2
    visto: start server
    extra: ensure ready, handle user input

T3
    visto: restart lsp for workspace, notify lsp status, set lsp status callback
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  start server → health ok
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → ensure ready
  T2 → handle user input
  T3 → on lsp missing install
  T3 → on workspace opened
