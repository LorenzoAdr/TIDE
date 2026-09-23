### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor del editor al cerrar el proyecto
keep: M1 M7
leído: reset to single cursor
Cerrado:
leído: reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al abrir un proyecto
keep: M1 M5
leído: reopen workspace documents, run, restore workspace session
Cerrado:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos al abrir un proyecto ocurre en restore workspace session 1395 1416 . Esta función carga un Workspace Session desde el disco load itera sobre session open tabs y llama a workspace open file para cada archivo válido. También restaura la pestaña activa si existe. El flujo de disparo se confirma en los callers: es invocada por set workspace y por el constructor Application lo que cubre tanto la apertura explícita de un proyecto como la restauración de sesión al inicio.
Abierto:
- restart lsp for workspace
- open file


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: reset to single cursor
    extra: set pos, handle shortcuts settings keys

T3
    visto: reopen workspace documents, run, restore workspace session
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  Make Main Layout → Make Editor Panel → replace text range with caret → reset to single cursor
hacia el resto:
  T2 → set pos
  T2 → handle shortcuts settings keys
  T3 → restart lsp for workspace
  T3 → on lsp missing install
