### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M8
leído: ensure completion server, ensure ready, start completion server, ready, health ok, stop owned unlocked, stop listener on port
Cerrado:
encontré el mecanismo: la detección de caída no-respuesta es health ok probe HTTP health y el disparo de reinicio ocurre en start completion server cuando health ok falla o el stamp cambia, invocando stop listener on port antes de relanzar
Abierto:
- start server
- handle route
- run level1 async

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje cuando se detecta que se ha caído
keep: M2 M5
leído: ensure completion server, ensure ready, start completion server
Cerrado:
leído: ensure completion server, ensure ready, start completion server
Abierto:
- handle route
- run level1 async
- stop owned unlocked
- stop listener on port

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje cuando el probe de salud falla o el proceso se cae
keep: M10 M2
leído: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ensure completion server, ensure ready, start completion server, ready
    extra: Model Store, default cache dir
    entre interno: ensure completion server → stop owned unlocked

T2
    visto: ensure completion server, ensure ready, start completion server
    extra: Model Store, default cache dir

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: ensure completion server
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Model Store
  T1 → default cache dir
  T2 → Model Store
  T2 → default cache dir
  T3 → on lsp missing install
  T3 → on workspace opened
