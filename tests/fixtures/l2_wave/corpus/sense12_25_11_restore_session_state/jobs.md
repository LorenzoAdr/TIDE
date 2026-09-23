### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, editor state hpp, open file at confirmed
Cerrado:
leído: Cursor Pos, open file at impl, editor state hpp, open file at confirmed
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
keep: M1
leído: open file at impl, open file at confirmed, editor state hpp
Cerrado:
leído: open file at impl, open file at confirmed, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2
leído: main layout cpp, main layout hpp
Cerrado:
leído: main layout cpp, main layout hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor, open file impl
Cerrado:
leído: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor, open file impl
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, open file at impl, editor state hpp, open file at confirmed
    extra: cursor selection span on line, ensure cursors

T2
    visto: open file at impl, open file at confirmed, editor state hpp
    extra: reset to single cursor, flush active tab

T3
    visto: main layout cpp, main layout hpp

T4
    visto: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: open file at impl
  T1=>T3  sin camino
  T1=>T4  mismo objeto: Cursor Pos
  T2=>T3  sin camino
  T2=>T4  mismo objeto: open file at impl
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → flush active tab
  T4 → cursor selection span on line
  T4 → ensure cursors
