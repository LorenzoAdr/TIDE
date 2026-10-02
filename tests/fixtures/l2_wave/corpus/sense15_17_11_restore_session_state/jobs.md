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
leído: save, remember, save workspace session, flush active tab
Cerrado:
leído: save, remember, save workspace session, flush active tab
Abierto:
- run custom event drain

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1 M7
leído: restore workspace session, open file, set workspace
Cerrado:
leído: restore workspace session, open file, set workspace
Abierto:
- handle navigation
- begin workspace bootstrap


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: save, remember, save workspace session, flush active tab
    extra: make intercept tool name, serialize ai settings

T3
    visto: restore workspace session, open file, set workspace
    extra: Application, is regular file

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  set workspace → save workspace session
hacia el resto:
  T2 → make intercept tool name
  T2 → serialize ai settings
  T3 → Application
  T3 → is regular file
