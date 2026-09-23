### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M7
leído: handle ai console keys, handle console panel mouse, clear pending insert, cancel current
Cerrado:
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if que invoca a cancel current para cancelar la generación. El clic fuera se gestiona en handle console panel mouse pero el cuerpo leído muestra lógica de selección y activación de input, sin una llamada explícita a cancelación visible en el fragmento truncado aunque el circuito sugiere que el flujo de ratón puede derivar en cancelación vía otros caminos no leídos en esta ola, la captura de Escape está confirmada y anclada .
Abierto:
- handle route
- handle user input
- handle console panel mouse cancel path

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la generación
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la generación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, cancel current
    extra: has pending insert, ai tab active

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
