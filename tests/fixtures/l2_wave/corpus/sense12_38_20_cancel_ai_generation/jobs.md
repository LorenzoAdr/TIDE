### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la generación; lo leído muestra que la cancelación se hace exclusivamente vía texto cancel, cancel, cancelar en is cancel input y handle user input, y que ai trace escape es solo un utilitario de string, no de eventos
Abierto:
- handle ai console keys
- handle key event
- on mouse click

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA
keep: M8
leído: clear pending insert, cancel current
Cerrado:
encontré dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA: en clear pending insert 766 770, que resetea pending insert y insert anchor y es invocado por cancel current cuando la inserción está pendiente y no hay actividad ocupada
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se procesa el evento de teclado para detectar la tecla Escape en la interfaz
keep: M9
leído: skip escape, handle csi
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el procesamiento del evento de teclado para detectar la tecla Escape en la interfaz; lo leído muestra que skip escape y handle csi procesan secuencias de escape ANSI CSI para renderizado de terminal, no eventos de teclado de la UI
Abierto:
- handle key event
- on key press
- ui event loop

### Trabajo 4
consulta: dónde se detecta la tecla Escape o el clic fuera para invocar la cancelación de la IA
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta la tecla Escape o el clic fuera para invocar la cancelación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

T3
    visto: skip escape, handle csi
    extra: clear to end of line, apply sgr

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T2 → cancel all
  T3 → clear to end of line
  T3 → apply sgr
