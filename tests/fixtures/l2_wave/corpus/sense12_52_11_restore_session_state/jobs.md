### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: seek, load working lines from disk
Cerrado:
leído: seek, load working lines from disk
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
keep: M2 M5
leído: load, save, reopen workspace documents, process pending workspace load, remember, set workspace, save workspace session, restore workspace session
Cerrado:
encontré el mecanismo completo: la lista de archivos abiertos se guarda en save workspace session que serializa workspace tabs a open tabs y llama a session save y se restaura en restore workspace session que carga la sesión y abre los archivos vía workspace open file . Ambos son invocados por set workspace al abrir el proyecto.
Abierto:
- run background generation
- run custom event drain

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M10
leído: workspace session hpp, save workspace session, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia de la visibilidad de los paneles laterales; lo leído Workspace Session solo serializa pestañas abiertas, argumentos de lanzamiento y programas de depuración, sin ningún campo para el estado de los paneles visible oculto
Abierto:
- run custom event drain
- visibility state
- serialize panels
- panel visibility


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: seek, load working lines from disk
    extra: open git diff view, open git diff tab

T2
    visto: load, save, reopen workspace documents, process pending workspace load
    extra: ensure wake fd, open host pty

T3
    visto: workspace session hpp, save workspace session, restore workspace session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: save workspace session
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T2 → ensure wake fd
  T2 → open host pty
  T3 → run custom event drain
  T3 → apply pending connection
