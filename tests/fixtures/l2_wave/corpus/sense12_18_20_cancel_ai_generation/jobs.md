### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación de la IA; lo leído muestra que la cancelación se activa únicamente mediante comandos de texto cancel, cancel, cancelar en is cancel input, y que ai trace escape es una función de escape de strings, no de eventos de teclado
Abierto:
- handle ai console keys
- handle key event
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
