### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación; lo leído es un formateador de strings ai trace escape y el sistema de cancelación ai controller sin enlace de input
Abierto:
- input loop
- ui event handler
- key capture escape

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la generación de la IA
keep: M2 M3
leído: cancel level1, cancel current, clear pending insert, cancel all
Cerrado:
leído: cancel level1, cancel current, clear pending insert, cancel all
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se captura el clic fuera o la tecla Escape para cancelar la generación de la IA
keep: M1 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura el clic fuera o la tecla Escape para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel level1, cancel current, clear pending insert, cancel all
    extra: Ai Controller, handle user input

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → Ai Controller
  T2 → handle user input
