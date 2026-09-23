### Trabajo 1
consulta: dónde se cancela la generación de código y se limpia el archivo al pulsar Escape
keep: M2 M1
leído: cancel current, clear pending insert, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que el sistema solo reconoce los comandos de texto cancel, cancel o cancelar is cancel input, pero no hay evidencia de que la tecla Escape código de escape dispare esta lógica en handle user input
Abierto:
- handle route
- key handler
- on key press
- escape key

### Trabajo 2
consulta: dónde se captura la tecla Escape para cancelar la generación de código
keep: M6 M9
leído: handle ai console keys, cancel level1, 3020 3165, 3020 3139
Cerrado:
leído: handle ai console keys, cancel level1, 3020 3165, 3020 3139
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, clear pending insert, handle user input, is cancel input
    extra: handle route, cancel all

T2
    visto: handle ai console keys, cancel level1, 3020 3165, 3020 3139
    extra: clear pending insert, has pending insert

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle user input → handle route → cancel current → cancel all
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  handle ai console keys → clear pending insert
hacia el resto:
  T1 → handle route
  T1 → run insert async
  T2 → has pending insert
  T2 → ai tab active
  T3 → handle route
  T3 → Ai Controller
