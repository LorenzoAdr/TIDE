### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de código
keep: M5 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de código
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación
keep: M4 M5
leído: cancel level1, clear pending insert, cancel current, cancel all
Cerrado:
leído: cancel level1, clear pending insert, cancel current, cancel all
Abierto:
- handle route
- handle user input


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel level1, clear pending insert, cancel current, cancel all
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → Ai Controller
  T2 → handle user input
