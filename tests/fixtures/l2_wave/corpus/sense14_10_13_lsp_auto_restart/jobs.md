### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: qué evento o señal detecta que el servidor de lenguaje se ha caído y llama a la función de reinicio
keep: M1 M9
leído: ensure ready, start completion server, ensure completion server, health ok
Cerrado:
leído: ensure ready, start completion server, ensure completion server, health ok
Abierto:
- handle route
- run level1 async
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 3
consulta: dónde se detecta que el servidor LSP ha dejado de responder y se dispara el reinicio
keep: M3 M5
leído: lsp client cpp, schedule debounced lsp restart, wait response, reader loop, restart lsp for workspace, start, on transport reader eof, run
Cerrado:
leído: lsp client cpp, schedule debounced lsp restart, wait response, reader loop, restart lsp for workspace, start, on transport reader eof, run
Abierto:
- send request
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: ensure ready, start completion server, ensure completion server, health ok
    extra: handle user input, run level1 async

T3
    visto: lsp client cpp, schedule debounced lsp restart, wait response, reader loop
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → handle user input
  T2 → run level1 async
  T3 → apply workspace settings
  T3 → setup build environment watching
