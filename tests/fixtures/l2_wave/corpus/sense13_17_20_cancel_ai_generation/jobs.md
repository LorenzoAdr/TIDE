### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7
leído: handle ai console keys, handle console panel mouse, clear pending insert, cancel current
Cerrado:
encontré el objeto de la consulta: la captura de Escape y clic fuera para cancelar la generación está en handle ai console keys líneas 3020-3165 y handle console panel mouse líneas 1489-1927 . En handle ai console keys se detecta Escape y, si hay una inserción pendiente has pending insert se invoca clear pending insert para cancelar. En handle console panel mouse se detectan clics fuera de las zonas activas terminal, input box y se invoca cancel current en el Ai Controller que a su vez llama a clear pending insert o cancel all según el estado. El efecto de cancelación se implementa en clear pending insert y cancel current .
Abierto:
- handle route
- handle user input

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la limpieza del archivo a medias; lo leído son banderas de estado agent cancel, pending insert y logs, sin ninguna operación de E S sobre el buffer o archivo del editor
Abierto:
- handle route
- handle user input
- tasks cancel
- agent cancel effect on editor buffer

### Trabajo 3
consulta: dónde se limpia el buffer del editor o se deshace la inserción parcial al cancelar la generación
keep: M5 M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el buffer del editor o se deshace la inserción parcial al cancelar la generación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, cancel current
    extra: has pending insert, ai tab active

T2
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: clear pending insert
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
