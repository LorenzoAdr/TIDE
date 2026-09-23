### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto para restaurarlo al abrirlo
keep: M5 M11
leído: save workspace session, flush active tab, save
Cerrado:
leído: save workspace session, flush active tab, save
Abierto:
- run custom event drain
- run background generation

### Trabajo 2
consulta: dónde se restaura la posición del cursor y los archivos abiertos al cargar la sesión del workspace
keep: M6
leído: load, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la restauración de archivos abiertos y la pestaña activa ocurre en restore workspace session que lee open tabs y active tab path de load . Sin embargo, no encontré la restauración de la posición del cursor línea columna en el texto leído; load solo restaura rutas de archivos y pestañas, sin campos de cursor.
Abierto:
- run inotify loop
- cursor position
- open file

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al cargar la sesión del workspace
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al cargar la sesión del workspace
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, flush active tab, save
    extra: run custom event drain, apply pending connection

T2
    visto: load, restore workspace session
    extra: ensure wake fd, open host pty

T3
    visto: (nada)

entre abiertas:
  T1=>T2  restore workspace session → open file → flush active tab
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → ensure wake fd
  T2 → open host pty
