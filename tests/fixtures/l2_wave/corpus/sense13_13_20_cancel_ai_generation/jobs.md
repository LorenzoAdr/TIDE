### Trabajo 1
consulta: cómo se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M1 M5
leído: cancel current, handle user input, handle route, is cancel input, cancel all, clear pending insert
Cerrado:
leído: cancel current, handle user input, handle route, is cancel input, cancel all, clear pending insert
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: cómo se limpia el archivo o se deshacen los cambios pendientes al cancelar la generación de código
keep: M7
leído: clear pending insert, cancel current
Cerrado:
encontré el mecanismo de limpieza: al cancelar, cancel current invoca clear pending insert que resetea el flag pending insert y el ancla insert anchor y luego despierta el ciclo con wake para que el sistema sepa que ya no hay inserción pendiente.
Abierto:
- handle route
- handle user input


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, handle user input, handle route, is cancel input
    extra: run insert async, handle level2 harness

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
hacia el resto:
  T1 → run insert async
  T1 → handle level2 harness
