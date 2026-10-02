### Trabajo 1
consulta: dónde se guarda y restaura el estado de la sesión al abrir un proyecto
keep: M1 M5
leído: restore workspace session, load, save, config path
Cerrado:
leído: restore workspace session, load, save, config path
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo
keep: M1
leído: open file at impl, open file confirmed, open file impl, reset to single cursor
Cerrado:
encontré el mecanismo: la restauración de la posición del cursor al abrir un archivo ocurre en open file at impl que llama a buffer reset to single cursor tras abrir el archivo. Esta función es invocada por open file at confirmed .
Abierto:
- open file at confirmed

### Trabajo 3
consulta: dónde se restauran los archivos abiertos y las pestañas al cargar la sesión del workspace
keep: M9
leído: load
Cerrado:
encontré el mecanismo de restauración: load lee el JSON y reconstruye session open tabs y session active tab path . El cuerpo leído en la ola 2 muestra explícitamente el bucle sobre doc open tabs y la asignación de active tab path que es la respuesta directa a la consulta.
Abierto:
- reopen workspace documents


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restore workspace session, load, save, config path
    extra: Application, set workspace

T2
    visto: open file at impl, open file confirmed, open file impl, reset to single cursor
    extra: open file at confirmed, flush active tab

T3
    visto: load
    extra: open host pty, ensure wake fd

entre abiertas:
  T1=>T2  restore workspace session → open file → open file impl
  T1=>T3  mismo objeto: load
  T2=>T3  sin camino
hacia el resto:
  T1 → Application
  T1 → set workspace
  T2 → open file at confirmed
  T2 → flush active tab
  T3 → open host pty
  T3 → ensure wake fd
