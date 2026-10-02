### Trabajo 1
consulta: dónde se restaura la posición del cursor, los archivos abiertos y los paneles laterales al abrir un proyecto
keep: M2 M11
leído: load working lines from disk, load virtual text placeholder, load tabular placeholder, restore workspace session, open file, open file at impl, open file impl, switch to tab
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración completa de la posición del cursor ni de los paneles laterales; lo leído muestra que restore workspace session restaura los archivos abiertos y la pestaña activa, pero usa open file que no restaura cursor en lugar de open file at impl que sí lo haría, y no hay código leído que restaure el estado de los paneles laterales
Abierto:
- restore panels
- restore sidebar state

### Trabajo 2
consulta: dónde se restaura el estado de los paneles laterales al abrir una sesión de trabajo
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura el estado de los paneles laterales al abrir una sesión de trabajo
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir una sesión de trabajo
keep: M12
leído: load working lines from disk, load virtual text placeholder, load tabular placeholder, record jump, record cursor jump
Cerrado:
leído: load working lines from disk, load virtual text placeholder, load tabular placeholder, record jump, record cursor jump
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: load working lines from disk, load virtual text placeholder, load tabular placeholder, restore workspace session
    extra: open git diff view, open git diff tab

T2
    visto: (nada)

T3
    visto: load working lines from disk, load virtual text placeholder, load tabular placeholder, record jump
    extra: open git diff view, open git diff tab

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: load working lines from disk
  T2=>T3  sin camino
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T3 → open git diff view
  T3 → open git diff tab
