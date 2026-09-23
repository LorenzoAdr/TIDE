### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M2
leído: ai trace escape, clear hover
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la generación; lo leído son utilidades de escape de strings ai trace escape y limpieza de hover UI clear hover, sin invocación a cancel level1 ni clear
Abierto:
- handle console panel mouse

### Trabajo 2
consulta: dónde se captura el clic fuera para cancelar la generación de la IA
keep: M1 M8
leído: handle console panel mouse, cancel current
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura del clic fuera para cancelar la generación; lo leído es el cuerpo de handle console panel mouse que maneja selección y hover, pero no contiene la lógica de clic fuera ni la llamada a cancel current el port en el circuito es una hipótesis no leída
Abierto:
- handle user input
- handle route
- handle console panel mouse

### Trabajo 3
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape para cancelar la generación de la IA
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
    visto: handle console panel mouse, cancel current
    extra: Is Empty, app tab active

T3
    visto: (nada)

entre abiertas:
  T1=>T2  handle console panel mouse → cancel current → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → update ai result hover
  T2 → Is Empty
  T2 → app tab active
