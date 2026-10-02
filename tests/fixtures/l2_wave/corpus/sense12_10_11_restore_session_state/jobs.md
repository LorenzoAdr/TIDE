### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído es la lógica de manipulación de cursores en memoria normalización, selección, colapso sin ningún hook de cierre ni serialización
Abierto:
- on close
- save state
- close buffer

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl
Cerrado:
leído: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl
Abierto:
- handle editor mouse
- append menu item
- append doc comment items
- append debug watch items

### Trabajo 3
consulta: dónde se serializa el estado de la sesión al cerrar el proyecto para restaurar archivos y paneles
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se serializa el estado de la sesión al cerrar el proyecto para restaurar archivos y paneles
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
