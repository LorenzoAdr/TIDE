### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
keep: M2
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape para interrumpir IA; lo leído es una función de escape de caracteres para logs ai trace escape
Abierto:
- on key
- on mouse

### Trabajo 2
consulta: dónde se cancela la generación de código y se limpia el archivo para evitar dejarlo a medias
keep: M8 M12
leído: cancel current, cancel level1, clear pending insert, cancel all, handle user input, handle route
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo para evitar dejarlo a medias; lo leído muestra que cancel current y clear pending insert solo limpian flags en memoria pending insert, insert anchor y cancelan tareas, pero no tocan el sistema de archivos ni el buffer del editor
Abierto:
- run insert async
- tasks cancel

### Trabajo 3
consulta: dónde se limpia el buffer del editor o se deshace la inserción parcial al cancelar la generación
keep: M2 M5
leído: clamp tabular scroll, load tabular placeholder
Cerrado:
leído: clamp tabular scroll, load tabular placeholder
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, cancel level1, clear pending insert, cancel all
    extra: Ai Controller, Symbol Filter Runner

T3
    visto: clamp tabular scroll, load tabular placeholder
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → Make Editor Panel
  T3 → Make Main Layout
