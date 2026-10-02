### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M3
leído: handle editor escape, handle editor keys, Event
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape clic para cancelar la IA; lo leído es la lógica de Escape del editor cerrar modales, completado, selección sin invocación a cancel level1
Abierto:
- dispatch editor keys
- dispatch editor mouse
- cancel level1 callers

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3
leído: clear pending insert, cancel level1, cancel current, cancel all
Cerrado:
leído: clear pending insert, cancel level1, cancel current, cancel all
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se captura el clic fuera del editor para cancelar la generación de la IA
keep: M1 M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura el clic fuera del editor para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle editor escape, handle editor keys, Event
    extra: clear primary selection, clear snippet session

T2
    visto: clear pending insert, cancel level1, cancel current, cancel all
    extra: handle user input, handle route

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → clear primary selection
  T1 → clear snippet session
  T2 → handle user input
  T2 → handle route
