### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: status text, start completion server, health ok, ensure completion server
Cerrado:
encontré el mecanismo de detección y reinicio: health ok detecta la caída mediante un GET health timeout 2000ms o fallo HTTP y start completion server dispara el reinicio si health ok falla o el stamp cambia, reiniciando el proceso si el PID no existe o no responde
Abierto:
- start server
- http ensure connected unlocked
- http exchange unlocked


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: status text, start completion server, health ok, ensure completion server
    extra: cache dir, stop listener on port
hacia el resto:
  T1 → cache dir
  T1 → stop listener on port
