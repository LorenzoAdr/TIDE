### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: on closed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardado de posición al cerrar el editor; lo leído es el cierre del file picker un selector de archivos, no el editor de texto y el estado del cursor en memoria set pos, pero falta el enlace de persistencia al cierre del editor
Abierto:
- on close
- save state
- save editor state

### Trabajo 2
consulta: dónde se guarda la lista de archivos abiertos al cerrar el editor
keep: M1
leído: session path, load
Cerrado:
leído: session path, load
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda el estado de los paneles laterales al cerrar el editor
keep: M1 M7
leído: load working lines from disk, save workspace session, save
Cerrado:
leído: load working lines from disk, save workspace session, save
Abierto:
- run custom event drain
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on closed
    extra: jump to selected, cancel ctrl chord

T2
    visto: session path, load
    extra: ensure wake fd, open host pty

T3
    visto: load working lines from disk, save workspace session, save
    extra: open git diff view, open git diff tab

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → jump to selected
  T1 → cancel ctrl chord
  T2 → ensure wake fd
  T2 → open host pty
  T3 → open git diff view
  T3 → open git diff tab
