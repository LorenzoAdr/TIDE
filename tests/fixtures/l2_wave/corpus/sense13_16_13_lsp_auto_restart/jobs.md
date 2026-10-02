### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: start completion server, status text, ensure completion server, health ok
Cerrado:
leído: start completion server, status text, ensure completion server, health ok
Abierto:
- start server
- stop owned unlocked
- stop listener on port


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: start completion server, status text, ensure completion server, health ok
    extra: stop listener on port, stop owned unlocked
hacia el resto:
  T1 → stop listener on port
  T1 → stop owned unlocked
