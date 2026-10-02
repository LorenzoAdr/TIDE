### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino guardar y restaurar la posición del cursor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el objeto de la consulta; lo leído son getters setters de coordenadas primary line, set primary y lógica de selección, sin métodos de serialización guardar restaurar ni camino ellos
Abierto:
- save cursor state
- restore cursor state
- serialize cursor


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, set pos, set primary
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
