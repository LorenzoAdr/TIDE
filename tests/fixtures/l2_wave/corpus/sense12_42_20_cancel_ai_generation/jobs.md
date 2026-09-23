### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación; lo leído es un sistema de cancelación basado en comandos de texto cancel, cancel, cancelar procesados en handle user input y verificados por is cancel input, y funciones de escape de strings ai trace escape que no tienen relación con eventos de teclado.
Abierto:
- handle ai console keys
- on key event
- on mouse click

### Trabajo 2
consulta: dónde se cancela la generación de la IA al pulsar Escape o hacer clic fuera
keep: M8
leído: cancel level1, cancel current
Cerrado:
leído: cancel level1, cancel current
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: qué hace cancel level1 y cancel current para limpiar el archivo y detener la generación
keep: M8
leído: cancel level1, cancel current, clear pending insert, cancel all
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: cancel level1 setea el flag agent cancel para detener la generación; cancel current limpia el archivo pendiente clear pending insert si no está busy, o llama a cancel all que invoca cancel level1 y tasks cancel si está busy, despertando el ciclo con wake true
Abierto:
- handle user input
- handle route
- tasks cancel


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cycle level2 mode override, set level2 mode override

T2
    visto: cancel level1, cancel current
    extra: cancel all, Ai Controller

T3
    visto: cancel level1, cancel current, clear pending insert, cancel all
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T3  mismo objeto: cancel level1
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: cancel level1
hacia el resto:
  T1 → cycle level2 mode override
  T1 → set level2 mode override
  T2 → Ai Controller
  T3 → Ai Controller
  T3 → Symbol Filter Runner
