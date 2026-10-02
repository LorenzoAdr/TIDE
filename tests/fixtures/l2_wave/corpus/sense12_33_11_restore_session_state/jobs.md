### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, save state, load state
Cerrado:
leído: Cursor Pos, save state, load state
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la selección del cursor al cargar el estado del editor
keep: M1
leído: Cursor Pos, load state, set primary, set pos
Cerrado:
leído: Cursor Pos, load state, set primary, set pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura el estado de los paneles laterales al abrir un proyecto
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura el estado de los paneles laterales al abrir un proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la lista de archivos abiertos al cargar el estado del editor
keep: M1
leído: restore workspace session, load state, open file
Cerrado:
encontré el mecanismo: la lista de archivos abiertos se restaura en restore workspace session que itera sobre session open tabs y llama a workspace open file para cada uno.
Abierto:
- handle navigation


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, save state, load state
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, load state, set primary, set pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

T4
    visto: restore workspace session, load state, open file
    extra: Application, set workspace

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T1=>T4  mismo objeto: load state
  T2=>T3  sin camino
  T2=>T4  mismo objeto: load state
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T4 → Application
  T4 → set workspace
