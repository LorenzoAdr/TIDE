### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación de IA; lo leído muestra que la cancelación se realiza exclusivamente mediante comandos de texto cancel, cancel, cancelar en is cancel input y handle user input, y que ai trace escape es una función de escape de strings, no de manejo de eventos de teclado
Abierto:
- handle ai console keys
- on key event
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
