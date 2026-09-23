### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7 M5
leído: restart lsp for workspace, schedule debounced lsp restart, health ok
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, health ok
Abierto:
- start server

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje cuando falta o se cae
keep: M5 M6
leído: ensure ready, start server
Cerrado:
leído: ensure ready, start server
Abierto:
- run level1 async
- handle route
- stop owned unlocked

### Trabajo 3
consulta: dónde se verifica el estado de salud del servidor de lenguaje para decidir si se debe reiniciar
keep: M1 M5
leído: wait until healthy, ensure ready, health ok
Cerrado:
encontré el objeto de la consulta: la verificación del estado de salud se realiza en health ok mediante una petición HTTP GET a health . La decisión de reiniciar o fallar se toma en wait until healthy que llama a health ok en un bucle con timeout; si el servidor muere waitpid detecta terminación o se agota el tiempo, retorna false, lo que implica la necesidad de reinicio o fallo. No hay lógica explícita de reiniciar en estos cuerpos, sino de verificar y reportar estado .
Abierto:
- start server
- handle route
- run level1 async
- start completion server


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, health ok
    extra: on lsp missing install, on workspace opened

T2
    visto: ensure ready, start server
    extra: handle user input, run level1 async

T3
    visto: wait until healthy, ensure ready, health ok
    extra: start server, run level1 async

entre abiertas:
  T1=>T2  start server → health ok
  T1=>T3  mismo objeto: health ok
  T2=>T3  mismo objeto: ensure ready
  T2=>T3  start server → wait until healthy
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → handle user input
  T2 → run level1 async
  T3 → run level1 async
  T3 → handle route
