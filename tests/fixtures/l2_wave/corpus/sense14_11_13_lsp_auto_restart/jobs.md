### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde
keep: M5 M6
leído: ensure ready, ensure intent embeddings ready
Cerrado:
leído: ensure ready, ensure intent embeddings ready
Abierto:
- handle route
- run level1 async
- end download

### Trabajo 3
consulta: qué evento o fallo dispara la función de reinicio del servidor de lenguaje
keep: M1 M6
leído: ensure ready, http ensure connected unlocked, wait until healthy, stop owned unlocked
Cerrado:
encontré el disparador del reinicio: la condición if en start server embedding y start completion server llama invoca stop owned unlocked cuando el servidor ya está healthy o hay un stamp antiguo, indicando un reinicio por cambio de configuración o conflicto de puerto.
Abierto:
- start server
- handle route
- run level1 async
- start completion server


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: ensure ready, ensure intent embeddings ready
    extra: handle user input, run level1 async

T3
    visto: ensure ready, http ensure connected unlocked, wait until healthy, stop owned unlocked
    extra: handle user input, run level1 async

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: ensure ready
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → handle user input
  T2 → run level1 async
  T3 → handle user input
  T3 → run level1 async
