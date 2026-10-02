### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído ai trace escape es una función de escape de strings JSON, no un handler de eventos de UI
Abierto:
- handle route
- json escape

### Trabajo 2
consulta: dónde se limpia el archivo o se detiene la inserción de texto cuando se cancela la IA
keep: M8
leído: cancel current, clear pending insert, handle user input, handle route, is cancel input, cancel all, cancel level1
Cerrado:
encontré el mecanismo de limpieza y detención: cancel current llama a clear pending insert que limpia pending insert e insert anchor y a cancel all que llama a cancel level1 para detener la generación L1; la consulta original sobre Escape clic fuera no se cumple aquí porque is cancel input solo detecta comandos de texto cancel, pero el efecto de limpiar detener está anclado en estas funciones
Abierto:
- handle key event
- on mouse click


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel current, clear pending insert, handle user input, handle route
    extra: run insert async, handle level2 harness

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → run insert async
  T2 → handle level2 harness
