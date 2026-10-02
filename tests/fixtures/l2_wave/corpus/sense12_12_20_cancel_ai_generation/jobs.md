### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: cancel current, ai trace escape, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la captura de Escape o clic fuera; lo leído es un parser de texto que solo detecta comandos literales cancel, cancel, cancelar en la entrada de línea, sin manejo de eventos de teclado key events ni de ratón mouse clicks .
Abierto:
- handle ai console keys
- on key event
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, ai trace escape, handle user input, handle route
    extra: clear pending insert, cancel all
hacia el resto:
  T1 → clear pending insert
  T1 → cancel all
