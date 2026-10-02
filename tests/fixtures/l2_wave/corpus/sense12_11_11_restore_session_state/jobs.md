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
leído: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
Cerrado:
encontré el mecanismo de restauración: al abrir un archivo, open file at impl workspace model llama a buffer reset to single cursor tras cargar el contenido; reset to single cursor editor state limpia los cursores, crea uno nuevo y fija su posición con set pos . Si se abre sin coordenadas explícitas load buffer from disk se restaura a 0, 0 .
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
