### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: cancel level1
Cerrado:
leído: cancel level1
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3 M7
leído: cancel level1, cancel current, clear pending insert, cancel all, cancel inflight completion, begin insert at, rollback pending
Cerrado:
leído: cancel level1, cancel current, clear pending insert, cancel all, cancel inflight completion, begin insert at, rollback pending
Abierto:
- handle user input
- handle route
- send completion request
- send cancel

### Trabajo 3
consulta: hay camino capturar Escape o clic fuera y limpiar el archivo a medias
keep: M3 M10
leído: ai trace escape, skip escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de capturar Escape clic fuera para limpiar archivos a medias; lo leído son funciones de escape de strings ai trace escape y parsing de secuencias ANSI skip escape, que no gestionan estado de archivos ni limpieza
Abierto:
- handle csi
- cancel level1
- file cleanup on escape


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1
    extra: cancel all, Ai Controller

T2
    visto: cancel level1, cancel current, clear pending insert, cancel all
    extra: Ai Controller, handle user input
    entre interno: begin insert at → cancel inflight completion

T3
    visto: ai trace escape, skip escape
    extra: handle csi

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T3  cancel level1 → ai trace escape
  T2=>T3  cancel level1 → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T2 → Ai Controller
  T2 → handle user input
  T3 → handle csi
