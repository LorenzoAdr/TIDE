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
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel level1, cancel current, clear pending insert, cancel all, handle user input
Cerrado:
leído: cancel level1, cancel current, clear pending insert, cancel all, handle user input
Abierto:
- handle route

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel level1, cancel current, clear pending insert, cancel all
    extra: Ai Controller, handle route

T3
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: cancel level1
hacia el resto:
  T2 → Ai Controller
  T2 → handle route
  T3 → handle route
  T3 → Ai Controller
