### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M1 M2
leído: cancel level1, cancel current, clear pending insert, handle user input, handle route
Cerrado:
leído: cancel level1, cancel current, clear pending insert, handle user input, handle route
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se captura la pulsación de Escape o el clic fuera para interrumpir la IA
keep: M1 M7
leído: ai trace escape, handle editor escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de interrupción de la IA; lo leído son utilidades de escape de strings ai trace escape y manejo de UI del editor handle editor escape, pero ninguna cancela la generación de IA
Abierto:
- handle editor keys
- stop async
- cancel generation
- ai cancel

### Trabajo 3
consulta: dónde se limpia el archivo o se deshace la inserción pendiente al cancelar la generación
keep: M4 M6
leído: clear pending insert, cancel current, handle user input, is cancel input
Cerrado:
encontré el mecanismo de limpieza de la inserción pendiente al cancelar la generación; lo leído es el cuerpo de clear pending insert y su invocación desde handle user input y cancel current
Abierto:
- handle route
- cancel all


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, cancel current, clear pending insert, handle user input
    extra: cancel all, Ai Controller

T2
    visto: ai trace escape, handle editor escape
    extra: handle editor keys, clear primary selection

T3
    visto: clear pending insert, cancel current, handle user input, is cancel input
    extra: handle route, cancel all

entre abiertas:
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle route → run level1 async → Make Main Layout → Make Console Panel → handle ai console keys → handle user input
  T2=>T3  clear pending insert → pending insert → ai trace escape
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T2 → handle editor keys
  T2 → clear primary selection
  T3 → cancel all
  T3 → run insert async
