### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, ai controller hpp, cancel current
Cerrado:
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3045 del peek, bloque if que invoca a ai clear pending insert para cancelar la inserción pendiente o a cancel current vía handle user input o handle route en el circuito para cancelar la generación activa. El clic fuera no se captura explícitamente en este flujo de teclado, pero handle ai console keys gestiona el foco y la selección. El efecto de cancelación se ejecuta en cancel current que llama a cancel all si hay tarea activa.
Abierto:
- handle user input
- handle route
- cancel all

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert, cancel all, cancel level1, begin insert at, handle user input
Cerrado:
encontré el mecanismo: al cancelar la generación de IA rama de efecto, el archivo a medias se limpia en clear pending insert que resetea el flag pending insert y el ancla insert anchor a valores vacíos, evitando así la inserción del texto pendiente en el buffer del editor.
Abierto:
- handle route

### Trabajo 3
consulta: hay camino cancelar la generación y limpiar el archivo a medias
keep: M8
leído: handle ai console keys
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el camino de cancelación desde la consola; lo leído muestra que handle ai console keys no llama a cancel current ni a ningún verbo de cancelación, solo gestiona scroll y selección
Abierto:
- cancel current
- clear pending insert


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, ai controller hpp, cancel current
    extra: clear pending insert, has pending insert

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle route, Ai Controller

T3
    visto: handle ai console keys
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → clear pending insert
  T1=>T3  mismo objeto: handle ai console keys
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle route
  T2 → Ai Controller
  T3 → has pending insert
  T3 → ai tab active
