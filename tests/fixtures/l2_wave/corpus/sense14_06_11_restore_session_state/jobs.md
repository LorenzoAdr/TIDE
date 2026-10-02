### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M1 M8
leído: save state, serialize ai settings, serialize ui colors
Cerrado:
leído: save state, serialize ai settings, serialize ui colors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close, set pos
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído muestra que close solo limpia estado UI y set pos es una función de actualización en memoria sin efectos secundarios de I O
Abierto:
- save state
- save editor state

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M8 M11
leído: restore workspace session, open file
Cerrado:
leído: restore workspace session, open file
Abierto:
- handle navigation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state, serialize ai settings, serialize ui colors
    extra: state path, write file

T2
    visto: close, set pos
    extra: request start, open for pid

T3
    visto: restore workspace session, open file
    extra: Application, set workspace

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  restore workspace session → serialize ai settings
  T2=>T3  restore workspace session → last launch program → open launch wizard → prepare connection wizard → exit debug mode
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → request start
  T2 → open for pid
  T3 → Application
  T3 → set workspace
