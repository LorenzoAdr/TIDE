### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación; lo leído es la lógica de cancelación por texto cancel en el controlador de IA y funciones de escape de strings para trazas
Abierto:
- handle ai console keys
- handle key event
- on mouse click

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert, cancel all
Cerrado:
leído: cancel current, clear pending insert, cancel all
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert
Cerrado:
leído: cancel current, clear pending insert
Abierto:
- handle user input
- handle route

### Trabajo 4
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all
    extra: handle user input, handle route

T3
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel current
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Ai Controller
  T1 → run insert async
  T2 → Ai Controller
  T2 → Symbol Filter Runner
