### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 2
consulta: dónde se lee el estado del proceso del servidor para saber si está vivo o muerto
keep: M1
leído: process alive
Cerrado:
encontré el mecanismo: process alive lee el estado vía waitpid child pid, status, WNOHANG y actualiza child pid si el proceso ha terminado
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se usa el estado del proceso para decidir si reiniciar el servidor
keep: M7
leído: ensure completion server, restart lsp for workspace, ensure ready, start server, stop owned unlocked, health ok
Cerrado:
encontré el objeto de la consulta: la decisión de reiniciar se basa en el estado del proceso server pid 0 y su salud health ok en ensure ready L945 y start server L792
Abierto:
- run level1 async
- start completion server
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: status text, start completion server, ensure completion server, health ok
    extra: cache dir, stop listener on port

T2
    visto: process alive
    extra: clangd process alive

T3
    visto: ensure completion server, restart lsp for workspace, ensure ready, start server
    extra: Model Store, default cache dir
    entre interno: ensure completion server → ensure ready

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: ensure completion server
  T2=>T3  sin camino
hacia el resto:
  T1 → cache dir
  T1 → stop listener on port
  T2 → clangd process alive
  T3 → Model Store
  T3 → default cache dir
