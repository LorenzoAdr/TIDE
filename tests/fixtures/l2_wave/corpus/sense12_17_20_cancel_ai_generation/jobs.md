### Trabajo 1
consulta: dónde se cancela la generación de la IA al pulsar Escape
keep: M8
leído: cancel level1, cancel current, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la cancelación por tecla Escape; lo leído muestra que la cancelación se activa solo por comandos de texto cancel, cancel, cancelar en handle user input, sin lógica de detección de la tecla Escape
Abierto:
- handle route
- handle key event
- on key press
- escape key handler


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, cancel current, handle user input, is cancel input
    extra: cancel all, Ai Controller
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
