### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M4
leído: wait until healthy, start completion server, health ok, ensure completion server
Cerrado:
leído: wait until healthy, start completion server, health ok, ensure completion server
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje cuando se detecta que no está saludable
keep: M1 M6
leído: ensure ready, ensure backend ready, health ok, wait until healthy, run level1 async, handle route, start completion server, ensure completion server
Cerrado:
leído: ensure ready, ensure backend ready, health ok, wait until healthy, run level1 async, handle route, start completion server, ensure completion server
Abierto:
- start server
- handle user input
- begin thinking
- end download
- end thinking
- stop owned unlocked

### Trabajo 3
consulta: dónde se inicia el servidor de lenguaje al arrancar la aplicación o al detectar que no está saludable
keep: M6
leído: ensure completion server, ensure ready, start completion server, handle route, run level1 async, ensure backend ready, health ok
Cerrado:
encontré el mecanismo: el servidor se inicia en ensure completion server que es invocado por run level1 async y rutas L2 . La detección de salud ocurre en start completion server vía health ok si falla o el stamp cambia, se reinicia.
Abierto:
- handle user input
- start server


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, start completion server, health ok, ensure completion server
    extra: ensure ready, start server

T2
    visto: ensure ready, ensure backend ready, health ok, wait until healthy
    extra: handle user input, cli runnable
    entre interno: run level1 async → ensure backend ready

T3
    visto: ensure completion server, ensure ready, start completion server, handle route
    extra: Model Store, default cache dir
    entre interno: ensure completion server → run level1 async

entre abiertas:
  T1=>T2  mismo objeto: wait until healthy
  T1=>T3  mismo objeto: start completion server
  T2=>T3  mismo objeto: ensure ready
hacia el resto:
  T1 → start server
  T1 → sleep for
  T2 → handle user input
  T2 → cli runnable
  T3 → Model Store
  T3 → default cache dir
