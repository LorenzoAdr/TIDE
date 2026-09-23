### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir el proyecto
keep: M1
leído: Cursor Pos, save workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor al cerrar abrir el proyecto; lo leído muestra que save workspace session solo persiste rutas de tabs y argumentos de lanzamiento, y Cursor Pos es una estructura de datos sin lógica de serialización propia
Abierto:
- run custom event drain
- flush active tab

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M1
leído: clear tabs, open tabs mru, save workspace session
Cerrado:
leído: clear tabs, open tabs mru, save workspace session
Abierto:
- run custom event drain

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: M2
leído: main layout hpp
Cerrado:
leído: main layout hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, save workspace session
    extra: cursor selection span on line, ensure cursors

T2
    visto: clear tabs, open tabs mru, save workspace session
    extra: set welcome buffer, normalize path

T3
    visto: main layout hpp

entre abiertas:
  T1=>T2  mismo objeto: save workspace session
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → set welcome buffer
  T2 → normalize path
