### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al volver a abrir un proyecto
keep: M1 M4
leído: save workspace session, restore workspace session, flush active tab, open file, save, open file impl, switch to tab
Cerrado:
leído: save workspace session, restore workspace session, flush active tab, open file, save, open file impl, switch to tab
Abierto:
- run custom event drain
- handle navigation
- run background generation

### Trabajo 2
consulta: qué datos de la sesión se guardan y restauran al abrir un proyecto cursor, archivos, paneles
keep: M10 M8
leído: save, remember, load, workspace session hpp, recent projects hpp
Cerrado:
encontré el objeto de la consulta: los datos de sesión que se guardan y restauran están definidos en Workspace Session pestañas abiertas, pestaña activa, argumentos de lanzamiento, programas de lanzamiento adjunción y en Recent Projects lista de rutas de workspaces recientes . Los cuerpos de save y load confirman la serialización exacta de estos campos.
Abierto:
- run background generation

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al cargar la sesión
keep: M6 M7
leído: load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de restauración de visibilidad de paneles laterales al cargar la sesión; lo leído load solo restaura pestañas abiertas, pestaña activa y argumentos de lanzamiento, sin tocar estado de paneles ni visibilidad
Abierto:
- restore panel visibility
- panel layout state
- sidebar visibility


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, restore workspace session, flush active tab, open file
    extra: run custom event drain, apply pending connection

T2
    visto: save, remember, load, workspace session hpp
    extra: run background generation, update active environment

T3
    visto: load
    extra: ensure wake fd, open host pty

entre abiertas:
  T1=>T2  mismo objeto: save
  T1=>T3  sin camino
  T2=>T3  mismo objeto: load
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → run background generation
  T2 → update active environment
  T3 → ensure wake fd
  T3 → open host pty
