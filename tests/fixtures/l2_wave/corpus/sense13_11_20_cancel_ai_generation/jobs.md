### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
keep: M2
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de Escape clic para interrumpir IA; lo leído ai trace escape es una función de escape de caracteres en strings para logs, no de eventos de teclado
Abierto:
- on key
- on mouse

### Trabajo 2
consulta: dónde se cancela la generación de código para evitar dejar el archivo a medias
keep: M5
leído: cancel level1, cancel all
Cerrado:
leído: cancel level1, cancel all
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de código
keep: M6
leído: clear pending insert, cancel current, cancel all, cancel level1, tasks, tasks cancel
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1, tasks, tasks cancel
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel level1, cancel all
    extra: Ai Controller, Symbol Filter Runner

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: cancel level1
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → handle user input
  T3 → handle route
