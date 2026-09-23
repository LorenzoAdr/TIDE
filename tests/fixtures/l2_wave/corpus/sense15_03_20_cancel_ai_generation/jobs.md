### Trabajo 1
consulta: dónde se captura Escape o clic fuera para cancelar la generación de código
keep: M1 M7
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la generación; lo leído es una función de escape de strings ai trace escape y un método de cancelación lógica cancel level1 sin el handler de input asociado
Abierto:
- on key
- on mouse
- clear hover if

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6 M5
leído: cancel current, clear pending insert, cancel all, handle user input, cancel level1, is cancel input
Cerrado:
leído: cancel current, clear pending insert, cancel all, handle user input, cancel level1, is cancel input
Abierto:
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, clear pending insert, cancel all, handle user input
    extra: handle route, Ai Controller

entre abiertas:
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T2 → handle route
  T2 → Ai Controller
