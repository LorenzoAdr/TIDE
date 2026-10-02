### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni el clic fuera para cancelar la generación de IA; lo leído es una función de escape de strings ai trace escape y una función de cancelación interna cancel level1 que no muestran el disparador UI
Abierto:
- handle ai console keys
- handle key event
- on mouse click
- cancel current

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel level1, cancel current, cancel all
Cerrado:
leído: cancel level1, cancel current, cancel all
Abierto:
- handle user input


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1
    extra: cancel all, Ai Controller

T2
    visto: cancel level1, cancel current, cancel all
    extra: Ai Controller, handle user input

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T2 → Ai Controller
  T2 → handle user input
