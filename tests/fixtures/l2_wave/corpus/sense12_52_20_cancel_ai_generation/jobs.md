### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M7
leído: handle ai download confirm keys
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape clic fuera para cancelar la generación de IA; lo leído handle ai download confirm keys es para cancelar la confirmación de descarga de paquetes, no la generación activa
Abierto:
- ai generation cancel
- ai trace escape

### Trabajo 2
consulta: dónde se limpia el archivo o se detiene la inserción de código al cancelar la generación
keep: M6
leído: cancel current, clear pending insert, cancel all, cancel level1, begin insert at, handle user input, run insert async, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo o se detiene la inserción de código al cancelar la generación; lo leído muestra que cancel all solo limpian flags internos pending insert, agent cancel y solicitan cancelación al agente, pero no hay evidencia de limpieza del buffer de texto en el editor ni de detención de la escritura en el archivo
Abierto:
- handle route
- agent cancel effect on editor buffer
- tasks cancel

### Trabajo 3
consulta: dónde se limpia el buffer del editor o se detiene la escritura en el archivo al cancelar la generación
keep: M3 M11
leído: cancel level1
Cerrado:
leído: cancel level1
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai download confirm keys
    extra: Character, start ai package install

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle route, Ai Controller
    entre interno: begin insert at → run insert async

T3
    visto: cancel level1
    extra: cancel all, Ai Controller

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: cancel level1
hacia el resto:
  T1 → Character
  T1 → start ai package install
  T2 → handle route
  T2 → Ai Controller
  T3 → Ai Controller
