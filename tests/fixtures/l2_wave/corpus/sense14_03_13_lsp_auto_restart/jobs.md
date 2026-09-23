### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7
leído: wait until healthy, status text, health ok, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída del servidor de lenguaje LSP clangd para disparar reinicio; lo leído es la lógica de salud del servidor de completado L2 llama backend y un reinicio manual del LSP restart lsp for workspace que no contiene lógica de detección de fallo
Abierto:
- start server
- on connection lost
- clangd monitor

### Trabajo 2
consulta: dónde se maneja la pérdida de conexión o caída del servidor de lenguaje para disparar un reinicio
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se maneja la pérdida de conexión o caída del servidor de lenguaje para disparar un reinicio
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia el servidor de lenguaje y se establece la conexión para detectar fallos
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se inicia el servidor de lenguaje y se establece la conexión para detectar fallos
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, status text, health ok, restart lsp for workspace
    extra: ensure ready, start server

T2
    visto: (nada)

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure ready
  T1 → start server
