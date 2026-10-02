### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de captura de Escape clic para cancelar la generación de IA; lo leído ai trace escape es una función de escape de strings JSON, no un handler de eventos de UI
Abierto:
- handle ai cancel
- on escape key
- on click outside

### Trabajo 2
consulta: dónde se invoca la cancelación del agente de IA al recibir una señal de interrupción
keep: M8
leído: cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de captura de Escape o clic fuera para cancelar la generación de IA; lo leído muestra que la cancelación se invoca vía texto cancel en handle user input y se enruta por handle route, pero no hay handler de teclado Escape ni de ratón clic fuera en el código leído
Abierto:
- handle key event
- handle escape
- on mouse click

### Trabajo 3
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M5 M8
leído: handle ai console keys, Event, cancel current, handle user input, handle route
Cerrado:
leído: handle ai console keys, Event, cancel current, handle user input, handle route
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se limpia el archivo o se revierte la inserción pendiente al cancelar la generación de IA
keep: M8
leído: clear pending insert
Cerrado:
encontré el mecanismo de limpieza de la inserción pendiente: es clear pending insert en ai controller, que resetea pending insert e insert anchor y despierta el ciclo; no hay evidencia de limpieza de archivos en disco en este locus, solo estado en memoria
Abierto:
- handle route
- cancel all


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel level1, cancel current, handle user input, handle route
    extra: cancel all, Ai Controller

T3
    visto: handle ai console keys, Event, cancel current, handle user input
    extra: clear pending insert, has pending insert

T4
    visto: clear pending insert
    extra: cancel current, handle user input

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T4  cancel current → clear pending insert
  T3=>T4  handle ai console keys → clear pending insert
hacia el resto:
  T2 → cancel all
  T2 → Ai Controller
  T3 → has pending insert
  T3 → ai tab active
