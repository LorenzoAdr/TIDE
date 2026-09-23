### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se programa el reinicio
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se programa el reinicio
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje
keep: M3
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: qué evento o fallo dispara la programación del reinicio diferido del servidor de lenguaje
keep: M2 M8
leído: wait until healthy, schedule debounced lsp restart
Cerrado:
leído: wait until healthy, schedule debounced lsp restart
Abierto:
- start server


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: setup build environment watching, apply workspace settings

T3
    visto: wait until healthy, schedule debounced lsp restart
    extra: ensure ready, start server

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T2 → setup build environment watching
  T2 → apply workspace settings
  T3 → ensure ready
  T3 → start server
