### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código de la IA
keep: M8
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, cancel current, 3020 3165, 3020 3139, 3060 3100
Cerrado:
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, cancel current, 3020 3165, 3020 3139, 3060 3100
Abierto:
- handle route
- handle user input

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción pendiente al cancelar la generación de la IA
keep: M1
leído: cancel current, clear pending insert
Cerrado:
encontré el mecanismo de limpieza de la inserción pendiente: al cancelar, cancel current invoca clear pending insert que resetea el estado lógico pending insert false insert anchor y despierta el ciclo; no hay evidencia de limpieza de archivos en disco en esta rama, solo de estado en memoria
Abierto:
- handle user input
- handle route
- cancel all

### Trabajo 3
consulta: dónde se detiene la escritura en el archivo cuando se cancela la generación de la IA
keep: M2
leído: cancel current, clear pending insert, cancel all, busy, cancel level1, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se detiene la escritura en el archivo; lo leído muestra la cancelación del estado pendiente clear pending insert y del agente L1 cancel level1, pero no el mecanismo que detiene el flujo de escritura activa en el editor el stream de tokens o la inserción en curso .
Abierto:
- handle user input
- handle route
- tasks cancel
- agent cancel effect on stream
- editor write loop


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T2
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

T3
    visto: cancel current, clear pending insert, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: clear pending insert
  T1=>T3  mismo objeto: clear pending insert
  T1=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → ai tab active
  T1 → event is ctrl c
  T2 → handle user input
  T2 → handle route
  T3 → handle user input
  T3 → handle route
