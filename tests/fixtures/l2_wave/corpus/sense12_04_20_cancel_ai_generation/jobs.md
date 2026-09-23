### Trabajo 1
consulta: dónde se intercepta la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input, cancel all
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se intercepta la tecla Escape o el clic fuera para cancelar la generación de la IA
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
no encontré la interceptación de la tecla Escape ni del clic fuera para cancelar la generación; lo leído muestra que la cancelación se activa únicamente mediante comandos de texto cancel, cancel, cancelar en is cancel input, y que ai trace escape es una función de escape de strings, no de captura de eventos de teclado
Abierto:
- handle ai console keys
- handle key event
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: Ai Controller, clear pending insert
hacia el resto:
  T1 → Ai Controller
  T1 → clear pending insert
