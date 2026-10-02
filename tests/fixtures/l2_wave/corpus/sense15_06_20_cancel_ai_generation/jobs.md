### Trabajo 1
consulta: cómo se cancela la generación de código cuando se pulsa Escape
keep: M1 M5
leído: cancel current, cancel level1, cancel all, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape; lo leído muestra que is cancel input solo detecta comandos de texto cancel cancel cancelar y no el evento de teclado Escape. El circuito disparo está incompleto: falta el handler de teclado que invoque a cancel current o handle user input al pulsar Escape.
Abierto:
- handle route
- handle key event
- key handler
- on key press

### Trabajo 2
consulta: dónde se captura la pulsación de Escape para cancelar la generación de código
keep: M3 M7
leído: ai trace escape, cancel current, handle user input, handle route, handle editor escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de Escape para cancelar la generación de código; lo leído muestra que handle editor escape gestiona UI del editor modales, selección y handle user input procesa comandos de texto, pero falta el puente que detecte la tecla Escape en el panel de consola o en el foco activo durante la generación y delegue a cancel current
Abierto:
- handle editor keys
- handle console escape
- handle keys
- handle key

### Trabajo 3
consulta: dónde se detecta el clic fuera del editor para cancelar la generación de código
keep: M4 M12
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta el clic fuera del editor para cancelar la generación de código
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, cancel level1, cancel all, handle user input
    extra: handle route, clear pending insert

T2
    visto: ai trace escape, cancel current, handle user input, handle route
    extra: clear pending insert, cancel all

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → clear pending insert
  T1 → Ai Controller
  T2 → clear pending insert
  T2 → run insert async
