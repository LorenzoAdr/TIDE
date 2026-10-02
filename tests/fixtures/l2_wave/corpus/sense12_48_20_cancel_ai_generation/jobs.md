### Trabajo 1
consulta: dónde se cancela la generación de código cuando se pulsa Escape
keep: M8
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el manejador de la tecla Escape; lo leído es una función de escape de caracteres para logs ai trace escape, no el disparador de cancelación
Abierto:
- handle key event
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M5 M8
leído: handle ai console keys, cancel current, clear pending insert, 3020 3165, 3020 3139
Cerrado:
leído: handle ai console keys, cancel current, clear pending insert, 3020 3165, 3020 3139
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: cómo se limpia el archivo a medias al cancelar la generación de código
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, begin insert at
Cerrado:
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, begin insert at
Abierto:
- handle route
- handle user input

### Trabajo 4
consulta: dónde se detecta el clic fuera del editor para cancelar la generación
keep: M7 M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta el clic fuera del editor para cancelar la generación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: handle ai console keys, cancel current, clear pending insert, 3020 3165
    extra: has pending insert, ai tab active

T3
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

T4
    visto: (nada)

entre abiertas:
  T1=>T2  handle ai console keys → handle user input → ai trace escape
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T2 → has pending insert
  T2 → ai tab active
  T3 → handle user input
  T3 → handle route
