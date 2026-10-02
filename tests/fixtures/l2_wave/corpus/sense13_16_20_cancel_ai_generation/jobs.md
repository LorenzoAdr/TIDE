### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M3
leído: cancel level1, ai trace escape, clear hover if
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído son utilidades de escape de strings ai trace escape y gestión de hover UI clear hover if, sin invocación a cancel level1 ni lógica de cancelación
Abierto:
- handle problems scrollbar mouse
- cancel all
- clear hover state

### Trabajo 2
consulta: dónde se limpia el archivo para evitar que quede a medias al cancelar
keep: M6
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, ai trace escape, clear hover if
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T1 → handle problems scrollbar mouse
  T2 → handle user input
  T2 → handle route
