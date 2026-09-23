### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
Cerrado:
encontré el mecanismo de restauración: al abrir un archivo, open file at impl workspace model invoca a reset to single cursor editor state con la línea y columna deseadas, y esta función establece la posición del cursor principal mediante set pos y muestra el parpadeo.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al reabrir el proyecto
keep: M1
leído: open file at impl, load buffer from disk
Cerrado:
leído: open file at impl, load buffer from disk
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la visibilidad de los paneles laterales al reabrir el proyecto
keep: M2
leído: Ai Controller
Cerrado:
leído: Ai Controller
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T3
    visto: open file at impl, load buffer from disk
    extra: open file at confirmed, reset to single cursor

T4
    visto: Ai Controller
    extra: Make Busy Strip Placeholder, Main Layout State

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: open file at impl
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → open file at confirmed
  T3 → flush active tab
