### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído define la estructura de datos en memoria Editor Buffer, Multi Cursor y funciones de manipulación en tiempo de ejecución, pero no hay evidencia de serialización, escritura a disco o guardado de estado al cerrar.
Abierto:
- save editor state
- write cursor pos
- on editor close

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: context menu open editor background, context menu open editor symbol, open file at impl, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de la posición del cursor al abrir el editor: se realiza en open file at impl que llama a buffer reset to single cursor tras cargar el archivo, posicionando el cursor en la línea y columna proporcionadas
Abierto:
- handle editor mouse


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: context menu open editor background, context menu open editor symbol, open file at impl, reset to single cursor
    extra: handle editor mouse, is lsp trackable path

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → handle editor mouse
  T2 → is lsp trackable path
