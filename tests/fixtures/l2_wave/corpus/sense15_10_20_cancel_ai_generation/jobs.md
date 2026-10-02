### Trabajo 1
consulta: dónde se cancela la generación de la IA al pulsar Escape
keep: M5 M10
leído: cancel current, cancel level1, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que la cancelación se activa por texto cancel, cancel, cancelar en el handler de input de la consola handle user input, no por el evento de teclado Escape El circuito de Escape no está en ai controller
Abierto:
- handle key event

### Trabajo 2
consulta: dónde se detecta el clic fuera del editor para cancelar la generación de la IA
keep: M1 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta el clic fuera del editor para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3 M10
leído: cancel level1, cancel current, clear pending insert
Cerrado:
leído: cancel level1, cancel current, clear pending insert
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, cancel level1, handle user input, handle route
    extra: clear pending insert, cancel all

T2
    visto: (nada)

T3
    visto: cancel level1, cancel current, clear pending insert
    extra: cancel all, Ai Controller

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle user input → clear pending insert
  T2=>T3  sin camino
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T3 → cancel all
  T3 → Ai Controller
