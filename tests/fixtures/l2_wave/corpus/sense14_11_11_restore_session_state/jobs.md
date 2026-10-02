### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y volver a abrir el proyecto
keep: M1 M7
leído: save workspace session, flush active tab, save
Cerrado:
leído: save workspace session, flush active tab, save
Abierto:
- run custom event drain
- run background generation

### Trabajo 2
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir el proyecto
keep: M8 M12
leído: handle source panel event, Make File Tree Panel
Cerrado:
leído: handle source panel event, Make File Tree Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la posición del cursor y los archivos abiertos al abrir el proyecto
keep: M2 M7
leído: load, record jump, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré la restauración de archivos abiertos en restore workspace session lee load y abre tabs, pero NO encontré la restauración de la posición del cursor; lo leído en record jump es solo grabación de saltos, no restauración
Abierto:
- restore cursor position


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, flush active tab, save
    extra: run custom event drain, apply pending connection

T2
    visto: handle source panel event, Make File Tree Panel
    extra: Character, handle source scrollbar mouse

T3
    visto: load, record jump, restore workspace session
    extra: ensure wake fd, open host pty

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  restore workspace session → open file → flush active tab
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → Character
  T2 → handle source scrollbar mouse
  T3 → ensure wake fd
  T3 → open host pty
