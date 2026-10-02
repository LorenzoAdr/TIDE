### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, cancel current, clear pending insert, Event, handle user input
Cerrado:
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if tras la selección o en el flujo principal truncado, que invoca a clear pending insert o cancel current para abortar la generación. El clic fuera no se captura explícitamente en los nodos leídos para cancelar IA solo hay Take Focus en eventos Custom .
Abierto:
- handle route
- handle ai console keys

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la limpieza del archivo a medias; lo leído son banderas de cancelación en memoria agent cancel, pending insert y llamadas a tasks cancel, sin operaciones de E S de archivos unlink, remove, truncate en la rama de cancelación
Abierto:
- handle route
- handle user input
- tasks cancel
- agent cancel effect


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, cancel current, clear pending insert, Event
    extra: has pending insert, ai tab active

T2
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle route
  T2 → Ai Controller
