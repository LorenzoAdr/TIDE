### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la selección del cursor al abrir un archivo
keep: M1
leído: Cursor Pos, ensure cursors
Cerrado:
leído: Cursor Pos, ensure cursors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la lista de archivos abiertos al reabrir un proyecto
keep: M1
leído: editor state hpp, ensure cursors
Cerrado:
leído: editor state hpp, ensure cursors
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, ensure cursors
    extra: cursor selection span on line, start command

T4
    visto: editor state hpp, ensure cursors
    extra: set primary

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  mismo objeto: editor state hpp
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  extra toca visto: ensure cursors
  T3=>T4  mismo objeto: ensure cursors
hacia el resto:
  T1 → cursor selection span on line
  T1 → handle shortcuts settings keys
  T2 → cursor selection span on line
  T2 → handle shortcuts settings keys
  T3 → cursor selection span on line
  T3 → start command
