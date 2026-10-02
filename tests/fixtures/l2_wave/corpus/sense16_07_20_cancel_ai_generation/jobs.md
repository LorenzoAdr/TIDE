### Trabajo 1
consulta: dónde se captura Escape o clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: cancel current, ai trace escape, handle user input, handle route, is cancel input, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar; lo leído muestra que la cancelación se hace vía comando de texto cancel en handle user input y que ai trace escape es solo un formateador de strings, no un handler de eventos
Abierto:
- handle console panel mouse
- cancel all

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación de la IA
keep: M1 M7
leído: cancel current, clear pending insert, cancel all, cancel level1, begin insert at
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1, begin insert at
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, ai trace escape, handle user input, handle route
    extra: clear pending insert, cancel all

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
hacia el resto:
  T1 → run insert async
  T1 → handle level2 harness
  T2 → Ai Controller
  T2 → Symbol Filter Runner
