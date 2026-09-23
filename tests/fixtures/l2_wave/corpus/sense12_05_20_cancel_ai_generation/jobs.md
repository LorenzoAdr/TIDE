### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré la captura de Escape o clic fuera; lo leído es un manejador de texto de consola handle user input que solo cancela con comandos explícitos como cancel
Abierto:
- handle ai console keys
- handle key event
- on mouse click


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cycle level2 mode override, set level2 mode override
hacia el resto:
  T1 → cycle level2 mode override
  T1 → set level2 mode override
