### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación; lo leído son funciones de escape de strings ai trace escape, json escape y canceladores lógicos cancel all sin conexión a input de teclado
Abierto:
- on key press
- key event handler
- input loop

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la generación de la IA
keep: M2
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se captura la pulsación de Escape o el clic fuera para invocar la cancelación de la IA
keep: M5
leído: console panel hpp, console panel cpp, handle console panel mouse
Cerrado:
leído: console panel hpp, console panel cpp, handle console panel mouse
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: console panel hpp, console panel cpp, handle console panel mouse
    extra: Is Empty, app tab active

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  handle console panel mouse → cancel current → ai trace escape
  T2=>T3  handle console panel mouse → cancel current → clear pending insert
hacia el resto:
  T2 → handle user input
  T2 → handle route
  T3 → Is Empty
  T3 → app tab active
