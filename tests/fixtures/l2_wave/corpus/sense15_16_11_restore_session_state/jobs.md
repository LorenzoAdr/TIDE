### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
keep: M3 M7
leído: Make Editor Panel, reset helix overlay state, clear hover state
Cerrado:
leído: Make Editor Panel, reset helix overlay state, clear hover state
Abierto:
- handle editor keys

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo
keep: M1 M6
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la posición del cursor al abrir un archivo
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al iniciar la sesión
keep: M8 M1
leído: rebuild display, rebuild display locked
Cerrado:
leído: rebuild display, rebuild display locked
Abierto:
- run input sync drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Make Editor Panel, reset helix overlay state, clear hover state
    extra: Make Main Layout, ensure buffer

T2
    visto: (nada)

T3
    visto: rebuild display, rebuild display locked
    extra: set consumer active, run input sync drain

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → ensure buffer
  T3 → set consumer active
  T3 → run input sync drain
