### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor posición del cursor y archivos abiertos al cerrar y volver a abrir un proyecto
keep: M1 M11
leído: save state, serialize ai settings, serialize ui colors
Cerrado:
leído: save state, serialize ai settings, serialize ui colors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo o proyecto
keep: M2 M6
leído: open file at impl, open file impl, open file at confirmed, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de posición al abrir un archivo: open file at impl en workspace model llama a buffer reset to single cursor tras cargar el archivo, y reset to single cursor en editor state aplica la posición con cursors front . La consulta se refiere a la restauración al abrir; el flujo leído cubre la apertura con coordenadas explícitas. No he leído el mecanismo de restauración automática desde historial si existe al abrir sin coordenadas, pero el objeto de la consulta restauración al abrir está anclado en estas dos funciones.
Abierto:
- open file impl

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al volver a abrir un proyecto
keep: M1 M7
leído: load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de archivos abiertos; lo leído load solo restaura la lista de rutas de proyectos recientes, no el estado de los archivos abiertos en el editor
Abierto:
- open file impl
- open file at impl
- serialize ai settings


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state, serialize ai settings, serialize ui colors
    extra: state path, write file

T2
    visto: open file at impl, open file impl, open file at confirmed, reset to single cursor
    extra: flush active tab, normalize path

T3
    visto: load
    extra: open host pty, ensure wake fd

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → flush active tab
  T2 → normalize path
  T3 → open host pty
  T3 → ensure wake fd
