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
leído: editor state hpp, Cursor Pos
Cerrado:
leído: editor state hpp, Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir un archivo guardado
keep: M1
leído: Cursor Pos, editor state hpp, open file at impl, reset to single cursor
Cerrado:
encontré el mecanismo de restauración: open file at impl llama a reset to single cursor line, col tras cargar el archivo, y esta función limpia los cursores y fija la posición del cursor primario en la línea y columna dadas
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la lista de archivos abiertos al reabrir un proyecto
keep: M1
leído: editor state hpp, open file at impl
Cerrado:
leído: editor state hpp, open file at impl
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: editor state hpp, Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp, open file at impl, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T4
    visto: editor state hpp, open file at impl
    extra: open file at confirmed, reset to single cursor

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  mismo objeto: editor state hpp
  T2=>T3  mismo objeto: editor state hpp
  T2=>T4  mismo objeto: editor state hpp
  T3=>T4  mismo objeto: editor state hpp
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
