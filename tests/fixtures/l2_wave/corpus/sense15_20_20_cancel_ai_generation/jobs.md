### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
keep: M2 M7
leído: ai trace escape, handle console panel mouse
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para interrumpir la IA; lo leído es un escape de caracteres de string ai trace escape y un handler de ratón que gestiona selección y hover, pero no interrupción de generación
Abierto:
- stop generation
- handle key event

### Trabajo 2
consulta: dónde se cancela la generación de código y se limpia el archivo a medias
keep: M11 M12
leído: cancel current, cancel inflight completion, clear pending insert, cancel all, cancel level1, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación se dispara en handle user input handle route al detectar cancel o clic fuera si mapeado a esa ruta, ejecutando cancel current . Este método limpia el estado pendiente clear pending insert y, si hay generación activa, invoca cancel all que marca agent cancel y cancela tareas. La limpieza del archivo a medias corresponde a clear pending insert que resetea pending insert y insert anchor y la interrupción del agente vía cancel level1 tasks cancel . No se encontró lógica explícita de truncado de buffer de texto en el editor, solo la invalidación del estado de inserción pendiente.
Abierto:
- send completion request
- truncate buffer

### Trabajo 3
consulta: dónde se mapea la tecla Escape o el clic fuera a la ruta de cancelación de la generación
keep: M3 M7
leído: handle completion keys, handle editor keys, handle user input, handle route, is cancel input, cancel current
Cerrado:
encontré el objeto de la consulta: la tecla Escape se mapea a la cancelación en dos capas distintas. 1 En la UI del editor handle completion keys Escape cierra la lista de completados completion close . 2 En el controlador de IA handle user input la entrada de texto cancel detectada por is cancel input o la ruta Cancel Agent procesada en handle route invocan cancel current que a su vez llama a cancel all para detener la generación. El clic fuera no se mapea explícitamente a cancelación de IA en los símbolos leídos; handle completion keys solo maneja Escape para cerrar la UI de completado, y handle user input solo procesa comandos de texto.
Abierto:
- handle editor mouse
- handle mouse event


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, handle console panel mouse
    extra: Is Empty, app tab active

T2
    visto: cancel current, cancel inflight completion, clear pending insert, cancel all
    extra: completions at, async worker main

T3
    visto: handle completion keys, handle editor keys, handle user input, handle route
    extra: Character, event is completion

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → Is Empty
  T1 → app tab active
  T2 → completions at
  T2 → async worker main
  T3 → Character
  T3 → event is completion
