### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré el mecanismo de captura de Escape clic para cancelar la generación de IA; lo leído ai trace escape es una función de escape de strings JSON, no un handler de eventos de UI
Abierto:
- handle key event
- handle escape
- on mouse click

### Trabajo 2
consulta: dónde se inicia la cancelación de la generación de código cuando el usuario interactúa con la interfaz
keep: M5 M8
leído: handle ai console keys, cancel level1, cancel current, clear pending insert, Event, handle user input
Cerrado:
leído: handle ai console keys, cancel level1, cancel current, clear pending insert, Event, handle user input
Abierto:
- handle route

### Trabajo 3
consulta: cómo se limpia el archivo a medias cuando se cancela la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré el mecanismo de limpieza del archivo a medias; lo leído son banderas de estado agent cancel, pending insert y cancelación de tareas, pero no hay código que borre texto parcial del buffer o archivo
Abierto:
- handle route
- handle user input
- tasks cancel
- agent cancel consumer
- partial text cleanup


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: handle ai console keys, cancel level1, cancel current, clear pending insert
    extra: has pending insert, ai tab active

T3
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T2 → has pending insert
  T2 → ai tab active
  T3 → handle route
  T3 → Ai Controller
