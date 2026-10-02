### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de código
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape para cancelar la generación de código
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación; lo leído ai trace escape es una función de escape de strings JSON, no un handler de teclado
Abierto:
- handle ai console keys
- cancel generation

### Trabajo 2
consulta: dónde se maneja la tecla Escape en el panel de consola para cancelar la IA
keep: M5
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Cerrado:
encontré el mecanismo: en handle ai console keys ola 2, la tecla Escape Escape se maneja en dos ramas. Si hay una selección de IA activa, la cancela clear terminal selection . Si no, y está abierto el menú de flujo de trabajo ai workflow menu open lo cierra. Si la IA está ocupada ai busy invoca ai cancel current ola 4, que a su vez llama a cancel all para detener la generación. El efecto de cancelación se propaga desde la consola al controlador de IA.
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se detecta el clic fuera del editor para cancelar la generación de IA
keep: M7
leído: Make Editor Panel, cancel current
Cerrado:
leído: Make Editor Panel, cancel current
Abierto:
- handle user input

### Trabajo 4
consulta: dónde se limpia el archivo a medias al cancelar la generación de IA
keep: M8
leído: cancel level1, cancel current, cancel all, clear pending insert, tasks, tasks cancel
Cerrado:
leído: cancel level1, cancel current, cancel all, clear pending insert, tasks, tasks cancel
Abierto:
- handle user input
- handle route


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: handle ai console keys, clear pending insert, has pending insert, cancel current
    extra: ai tab active, event is ctrl c

T3
    visto: Make Editor Panel, cancel current
    extra: Make Main Layout, ensure buffer

T4
    visto: cancel level1, cancel current, cancel all, clear pending insert
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle ai console keys → handle user input → handle route → run level1 async → invoke tool logged → Make Main Layout
  T2=>T4  mismo objeto: clear pending insert
  T2=>T4  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T3=>T4  mismo objeto: cancel current
hacia el resto:
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → Make Main Layout
  T3 → ensure buffer
  T4 → Ai Controller
  T4 → handle user input
