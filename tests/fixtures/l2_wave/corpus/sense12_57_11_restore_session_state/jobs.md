### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1
leído: save state, serialize ai settings, serialize ui colors
Cerrado:
leído: save state, serialize ai settings, serialize ui colors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura el estado del editor al abrir el proyecto
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura el estado del editor al abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: save state, serialize ai settings, serialize ui colors
    extra: state path, write file

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
