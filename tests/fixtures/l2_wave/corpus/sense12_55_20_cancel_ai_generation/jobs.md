### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape; lo leído es un formateador de strings ai trace escape y un controlador de cancelación ai controller que no muestra el handler de input
Abierto:
- key handler
- on key press
- escape key

### Trabajo 2
consulta: dónde se cancela la generación de la IA al hacer clic fuera del editor
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se cancela la generación de la IA al hacer clic fuera del editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se invoca la cancelación del agente de nivel 1 al pulsar Escape
keep: M1 M9
leído: handle editor escape, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la invocación de cancelación del agente de nivel 1 al pulsar Escape; lo leído es el cuerpo de handle editor escape que solo gestiona UI mouse, modals, completion, find, multi-cursor, selection y el cuerpo de cancel level1 que es el efecto pero no muestra el caller de input
Abierto:
- handle editor keys
- cancel all


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: (nada)

T3
    visto: handle editor escape, cancel level1
    extra: handle editor keys, end mouse selection

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  cancel level1 → ai trace escape
  T2=>T3  sin camino
hacia el resto:
  T3 → handle editor keys
  T3 → end mouse selection
