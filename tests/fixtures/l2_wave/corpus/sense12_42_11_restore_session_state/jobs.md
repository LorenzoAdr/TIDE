### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos, save workspace session, flush active tab, save
Cerrado:
leído: Cursor Pos, save workspace session, flush active tab, save
Abierto:
- run custom event drain

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos, save workspace session, flush active tab, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de la posición del cursor al abrir el proyecto; lo leído restore workspace session restaura las pestañas abiertas y la pestaña activa, pero no la posición del cursor dentro del archivo
Abierto:
- run custom event drain

### Trabajo 3
consulta: dónde se guarda el estado de los paneles laterales al cerrar el proyecto
keep: M5 M6
leído: 1043
Cerrado:
leído: 1043
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, save workspace session, flush active tab, save
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, save workspace session, flush active tab, restore workspace session
    extra: cursor selection span on line, ensure cursors

T3
    visto: 1043
    extra: Make Console Panel, Make Outline Panel

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → Make Console Panel
  T3 → Make Outline Panel
