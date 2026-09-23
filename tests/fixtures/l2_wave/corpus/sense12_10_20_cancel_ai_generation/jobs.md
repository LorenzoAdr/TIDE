### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación de la IA; lo leído es la cancelación por comando de texto cancel, cancel, cancelar en is cancel input y el efecto de cancelación en cancel level1
Abierto:
- handle ai console keys
- handle key event
- handle escape
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
