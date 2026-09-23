### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir el proyecto
keep: M2
leído: save workspace session
Cerrado:
leído: save workspace session
Abierto:
- run custom event drain

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M5 M6
leído: load, save, remember
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la lista de archivos abiertos; lo leído recent projects solo persiste rutas de directorios de trabajo workspaces, no archivos individuales
Abierto:
- begin shutdown
- process pending workspace load

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: M11
leído: panel hpp
Cerrado:
leído: panel hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session
    extra: run custom event drain, apply pending connection

T2
    visto: load, save, remember
    extra: ensure wake fd, open host pty

T3
    visto: panel hpp, layout state

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → ensure wake fd
  T2 → open host pty
