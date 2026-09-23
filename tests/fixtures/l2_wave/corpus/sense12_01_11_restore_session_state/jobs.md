### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp, Make Editor Panel
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda la posición del cursor al cerrar el editor
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: Cursor Pos, editor state hpp, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: context menu open editor background, context menu open editor symbol
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la posición del cursor al abrir el editor
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: context menu open editor background, context menu open editor symbol
Abierto:
- handle editor mouse
- append menu item
- append doc comment items
- append debug watch items


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp, Make Editor Panel
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
