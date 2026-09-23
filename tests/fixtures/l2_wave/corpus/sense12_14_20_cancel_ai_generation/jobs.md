### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la captura de la tecla Escape ni el clic fuera para cancelar la generación; lo leído es una función de escape de caracteres para strings ai trace escape y una función de cancelación interna cancel level1 que no muestra el evento de entrada
Abierto:
- handle ai console keys
- on key press
- on mouse click
- cancel current


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1
    extra: cancel all, Ai Controller
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
