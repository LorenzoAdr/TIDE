### Trabajo 1
consulta: dónde se captura la pulsación de Escape para cancelar la generación de la IA
keep: M1 M2
leído: ai trace escape, cancel current, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la pulsación de Escape; lo leído muestra que la cancelación se activa mediante comandos de texto cancel, cancel, cancelar en is cancel input, no por eventos de teclado
Abierto:
- handle route
- handle key event
- on key press
- key handler

### Trabajo 2
consulta: dónde se limpia la inserción pendiente cuando se cancela la generación de la IA
keep: M2 M1
leído: clear pending insert, cancel current
Cerrado:
encontré dónde se limpia la inserción pendiente: en clear pending insert invocado por cancel current cuando pending insert es true y no hay actividad ocupada
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
    visto: ai trace escape, cancel current, handle user input, is cancel input
    extra: handle route, clear pending insert

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → handle route
  T1 → cancel all
  T2 → handle route
  T2 → cancel all
