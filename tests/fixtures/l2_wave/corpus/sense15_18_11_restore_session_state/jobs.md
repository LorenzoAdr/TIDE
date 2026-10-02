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
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M1 M6
leído: reset to single cursor, reset helix editor state, open file at impl, load working lines from disk
Cerrado:
leído: reset to single cursor, reset helix editor state, open file at impl, load working lines from disk
Abierto:
- handle editor keys

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: M1 M6
leído: reopen workspace documents, run, load, on workspace complete
Cerrado:
leído: reopen workspace documents, run, load, on workspace complete
Abierto:
- restart lsp for workspace
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: reset to single cursor, reset helix editor state, open file at impl, load working lines from disk
    extra: set pos, start command

T3
    visto: reopen workspace documents, run, load, on workspace complete
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  Make Main Layout → Make Editor Panel → replace text range with caret → reset to single cursor
hacia el resto:
  T2 → set pos
  T2 → start command
  T3 → restart lsp for workspace
  T3 → on lsp missing install
