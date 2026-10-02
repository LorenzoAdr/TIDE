### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M2
leído: ai trace escape, clear hover
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído es un escape de strings ai trace escape y un clear de hover UI clear hover, sin invocación a cancel level1 ni lógica de cancelación
Abierto:
- handle console panel mouse
- cancel level1 callers

### Trabajo 2
consulta: dónde se cancela la generación de la IA cuando se pulsa Escape o se hace clic fuera
keep: M2 M3
leído: cancel current, cancel level1, handle user input, handle route, is cancel input, handle editor keys, handle editor mouse
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído es un escape de strings ai trace escape y un clear de hover UI clear hover, sin invocación a cancel level1 ni lógica de cancelación
Abierto:
- cancel ai

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
    visto: ai trace escape, clear hover
    extra: update ai result hover, handle console panel mouse

T2
    visto: cancel current, cancel level1, handle user input, handle route
    extra: clear pending insert, cancel all

T3
    visto: (nada)

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → update ai result hover
  T1 → handle console panel mouse
  T2 → clear pending insert
  T2 → cancel all
