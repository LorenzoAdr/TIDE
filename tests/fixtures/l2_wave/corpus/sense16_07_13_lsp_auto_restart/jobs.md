### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7 M5
leído: wait until healthy, status text, health ok, run level1 async, ensure backend ready, ensure ready
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída en tiempo de ejecución ni el disparo de reinicio; lo leído es arranque inicial ensure ready y espera de salud tras arranque health ok, que no cubren el caso de un servidor ya vivo que se cae
Abierto:
- handle route
- start server
- handle user input
- restart server
- health monitor
- on server exit

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje tras una caída o fallo
keep: M3 M11
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el proceso del servidor LSP ha terminado inesperadamente o ha muerto
keep: M7 M4
leído: wait fd readable, stop, reader loop, read message, restart lsp for workspace, schedule debounced lsp restart, start, run
Cerrado:
encontré el mecanismo de detección: en reader loop cuando read message devuelve nullopt por EOF error proceso muerto, se invoca reader eof handler . Este handler está registrado en application no leído el cuerpo de registro, pero el flujo es claro y dispara schedule debounced lsp restart que a su vez lleva a restart lsp for workspace .
Abierto:
- register lsp eof handler


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, status text, health ok, run level1 async
    extra: start server, handle route

T2
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T3
    visto: wait fd readable, stop, reader loop, read message
    extra: poll runner, Workspace Indexer

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → start server
  T1 → handle route
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → poll runner
  T3 → Workspace Indexer
