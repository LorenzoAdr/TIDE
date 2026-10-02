### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído es una función de escape de strings para logs ai trace escape, no un handler de input
Abierto:
- handle escape
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la generación de código
keep: M6
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: cancel level1, cancel all

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: cancel all
hacia el resto:
  T2 → handle user input
  T2 → handle route
