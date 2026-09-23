### Trabajo 1
consulta: dónde se parsean los errores de compilación de la consola para extraer la línea y el mensaje
keep: M1 M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación de la consola para extraer la línea y el mensaje
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor
keep: M1 M4
leído: line gutter marker, gutter buffer line at row
Cerrado:
leído: line gutter marker, gutter buffer line at row
Abierto:
- handle editor mouse


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → Make Editor Panel
  T2 → Make Main Layout
