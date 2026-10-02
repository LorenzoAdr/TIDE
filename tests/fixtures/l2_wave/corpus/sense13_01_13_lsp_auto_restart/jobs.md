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
consulta: dónde se programa el reinicio diferido del servidor de lenguaje
keep: M3
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se gestiona el ciclo de vida del proceso del servidor de lenguaje para añadir la detección de caída
keep: M5
leído: ensure completion server, start completion server, stop owned unlocked, stop listener on port, health ok
Cerrado:
encontré el objeto de la consulta: la gestión del ciclo de vida y la detección de caída health check se gestionan en start completion server que lanza el proceso y verifica health ok y health ok que realiza la petición HTTP al endpoint health . La detección de caída ya existe mediante health ok que se invoca tras el fork para confirmar que el servidor está listo y es el mecanismo a extender o conectar para la detección continua.
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
    visto: ensure completion server, start completion server, stop owned unlocked, stop listener on port
    extra: Model Store, default cache dir
    entre interno: ensure completion server → stop owned unlocked

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → setup build environment watching
  T2 → apply workspace settings
  T3 → Model Store
  T3 → default cache dir
