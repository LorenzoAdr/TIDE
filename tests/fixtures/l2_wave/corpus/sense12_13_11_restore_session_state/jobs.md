### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se persiste el rango de selección del cursor al salir
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se escribe la posición del cursor al salir del editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la posición del cursor al abrir un archivo guardado
keep: M1
leído: Cursor Pos, open file at impl, reset to single cursor, open file at confirmed
Cerrado:
encontré el mecanismo de restauración: la posición se restaura pasando line y col desde open file at confirmed hacia open file at impl que invoca reset to single cursor en el buffer del editor. El efecto es la creación de un cursor único en esa posición. No se lee persistencia en esta rama, solo el flujo de parámetros.
Abierto:
- open file at confirmed caller


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T4
    visto: Cursor Pos, open file at impl, reset to single cursor, open file at confirmed
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  mismo objeto: Cursor Pos
  T3=>T4  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
