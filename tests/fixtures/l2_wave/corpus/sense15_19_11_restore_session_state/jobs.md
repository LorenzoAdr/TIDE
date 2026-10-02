### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1 M12
leído: save, save workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se guarda la posición del cursor al cerrar el proyecto; lo leído save y save workspace session serializa solo open tabs, active tab path, launch args y last launch program, pero no row
Abierto:
- run background generation
- run custom event drain
- cursor col
- flush active tab

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1 M7
leído: restore workspace session, open file, workspace session hpp
Cerrado:
leído: restore workspace session, open file, workspace session hpp
Abierto:
- handle navigation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: save, save workspace session
    extra: run background generation, update active environment

T3
    visto: restore workspace session, open file, workspace session hpp
    extra: set workspace, Application

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  save workspace session → active tab path → open file
hacia el resto:
  T2 → run background generation
  T2 → update active environment
  T3 → set workspace
  T3 → Application
