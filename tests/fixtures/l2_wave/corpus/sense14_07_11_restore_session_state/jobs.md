### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor posición del cursor, archivos abiertos, paneles visibles al cerrar y abrir un proyecto
keep: M5 M9
leído: process pending workspace load, set workspace, resolve workspace for anchor
Cerrado:
leído: process pending workspace load, set workspace, resolve workspace for anchor
Abierto:
- begin workspace bootstrap

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la posición del cursor al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos y paneles visibles al iniciar el bootstrap del workspace
keep: M1 M9
leído: process pending workspace load, set workspace, resolve workspace for anchor, reopen workspace documents, reload stale tabs from disk
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de restauración de archivos y paneles en el bootstrap; lo leído muestra que set workspace limpia las pestañas clear tabs y guarda la sesión save workspace session, pero no las restaura, y reopen workspace documents solo notifica al LSP sin abrir archivos
Abierto:
- run custom event drain
- load workspace session
- restore workspace layout


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: process pending workspace load, set workspace, resolve workspace for anchor
    extra: run custom event drain, has value

T2
    visto: (nada)

T3
    visto: process pending workspace load, set workspace, resolve workspace for anchor, reopen workspace documents
    extra: run custom event drain, has value

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: process pending workspace load
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → has value
  T3 → run custom event drain
  T3 → has value
