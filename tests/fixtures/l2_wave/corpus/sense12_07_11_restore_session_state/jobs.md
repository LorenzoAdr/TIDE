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
leído: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl, reset to single cursor
Cerrado:
encontré el mecanismo: al abrir el editor, open file at impl invoca reset to single cursor que limpia los cursores y establece la posición primaria mediante set pos .
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se restaura la selección del cursor al abrir un archivo
keep: M1
leído: Cursor Pos, editor state hpp, reset to single cursor, open file at impl
Cerrado:
encontré el mecanismo de restauración: al abrir un archivo, open file at impl disparo invoca a reset to single cursor efecto sobre el buffer del editor, estableciendo la posición del cursor en la línea y columna proporcionadas.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se guarda el estado de los paneles laterales al cerrar la sesión
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda el estado de los paneles laterales al cerrar la sesión
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp, reset to single cursor, open file at impl
    extra: cursor selection span on line, ensure cursors

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  sin camino
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
