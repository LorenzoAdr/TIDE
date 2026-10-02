### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré dónde se guarda la posición del cursor al cerrar el editor; lo leído es la estructura de datos del cursor en memoria EditorBuffer CursorPos y funciones de manipulación en tiempo de ejecución, pero no hay evidencia de serialización, persistencia en disco o restauración al abrir.
Abierto:
- save editor state
- restore editor state
- on editor close

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: context menu open editor background, context menu open editor symbol
Cerrado:
leído: context menu open editor background, context menu open editor symbol
Abierto:
- handle editor mouse
- append menu item
- append doc comment items
- append debug watch items


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: context menu open editor background, context menu open editor symbol
    extra: handle editor mouse, append doc comment items

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → handle editor mouse
  T2 → append doc comment items
