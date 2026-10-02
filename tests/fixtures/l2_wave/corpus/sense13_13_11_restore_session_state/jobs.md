### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor text hpp, text input style hpp
Cerrado:
leído: editor text hpp, text input style hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al reabrir un proyecto
keep: M6 M2
leído: load, save, reopen workspace documents
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la lista de archivos abiertos; lo leído es la lista de proyectos recientes Recent Projects y una función de reapertura de documentos que itera sobre tabs ya existentes en memoria, sin persistencia propia
Abierto:
- run background generation
- restart lsp for workspace

### Trabajo 3
consulta: dónde se guarda y restaura el estado de los paneles laterales al reabrir un proyecto
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado de los paneles laterales al reabrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor text hpp, text input style hpp

T2
    visto: load, save, reopen workspace documents
    extra: ensure wake fd, open host pty

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → ensure wake fd
  T2 → open host pty
