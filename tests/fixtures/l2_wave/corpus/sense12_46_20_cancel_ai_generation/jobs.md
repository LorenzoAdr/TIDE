### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M2
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación; lo leído es una función de escape de caracteres JSON ai trace escape que no tiene relación con eventos de teclado ni cancelación de procesos
Abierto:
- handle key event
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación
keep: M8
leído: cancel current, clear pending insert, cancel level1, cancel all
Cerrado:
leído: cancel current, clear pending insert, cancel level1, cancel all
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M5 M8
leído: handle ai console keys, cancel level1, 3050 3100
Cerrado:
encontré la captura de la tecla Escape para cancelar la generación de la IA; lo leído es el bloque en handle ai console keys líneas 3050-3100 que detecta Escape y, si hay input activo y una inserción pendiente, llama a ai- clear pending insert
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se captura el clic fuera para cancelar la generación de la IA
keep: M5 M8
leído: handle ai console keys, cancel current
Cerrado:
leído: handle ai console keys, cancel current
Abierto:
- handle user input
- handle route


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape
    extra: cycle level2 mode override, set level2 mode override

T2
    visto: cancel current, clear pending insert, cancel level1, cancel all
    extra: handle user input, handle route

T3
    visto: handle ai console keys, cancel level1, 3050 3100
    extra: clear pending insert, has pending insert

T4
    visto: handle ai console keys, cancel current
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  handle ai console keys → handle user input → ai trace escape
  T1=>T4  handle ai console keys → handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current
  T2=>T4  mismo objeto: cancel current
  T2=>T4  handle ai console keys → clear pending insert
  T3=>T4  mismo objeto: handle ai console keys
hacia el resto:
  T1 → cycle level2 mode override
  T1 → set level2 mode override
  T2 → handle user input
  T2 → handle route
  T3 → has pending insert
  T3 → ai tab active
