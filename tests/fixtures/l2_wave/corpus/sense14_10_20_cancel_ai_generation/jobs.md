### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído es un formateador de strings ai trace escape y un gestor de cancelación lógica ai controller, pero no el handler de eventos de teclado
Abierto:
- handle escape
- on key press
- key event handler

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la inserción de código
keep: M6 M4
leído: clear pending insert, cancel current, cancel all, handle user input, cancel level1, run insert async
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo al cancelar la inserción; lo leído es la cancelación lógica del estado clear pending insert, cancel all sin mutación del buffer del editor
Abierto:
- handle route
- undo insert
- restore snapshot
- revert insert

### Trabajo 3
consulta: dónde se cancela la generación de la IA al pulsar Escape o hacer clic fuera
keep: M1 M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se cancela la generación de la IA al pulsar Escape o hacer clic fuera
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: clear pending insert, cancel current, cancel all, handle user input
    extra: handle route, Ai Controller

T3
    visto: (nada)

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → handle route
  T2 → Ai Controller
