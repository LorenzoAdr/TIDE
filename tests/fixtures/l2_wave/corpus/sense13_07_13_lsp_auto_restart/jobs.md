### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje cuando se detecta un fallo
keep: M2
leído: schedule debounced lsp restart, restart lsp for workspace, on lsp missing install
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se captura el fallo o la falta de respuesta del servidor de lenguaje para disparar el reinicio
keep: M1
leído: http exchange unlocked, http ensure connected unlocked, wait until healthy, complete server
Cerrado:
leído: http exchange unlocked, http ensure connected unlocked, wait until healthy, complete server
Abierto:
- send all


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, on lsp missing install
    extra: setup build environment watching, apply workspace settings

T3
    visto: http exchange unlocked, http ensure connected unlocked, wait until healthy, complete server
    extra: send all, c str

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → setup build environment watching
  T2 → apply workspace settings
  T3 → send all
  T3 → c str
