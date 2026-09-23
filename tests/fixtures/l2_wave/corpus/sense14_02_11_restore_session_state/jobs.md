### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M10
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la posición del cursor al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M4
leído: reopen workspace documents, run, open workspace wizard, Event
Cerrado:
leído: reopen workspace documents, run, open workspace wizard, Event
Abierto:
- restart lsp for workspace
- ensure backend started

### Trabajo 3
consulta: dónde se restauran los paneles laterales visibles al abrir un proyecto
keep: M4 M5
leído: on workspace opened
Cerrado:
leído: on workspace opened
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: reopen workspace documents, run, open workspace wizard, Event
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: on workspace opened

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  set workspace → on workspace opened
hacia el resto:
  T2 → restart lsp for workspace
  T2 → on lsp missing install
