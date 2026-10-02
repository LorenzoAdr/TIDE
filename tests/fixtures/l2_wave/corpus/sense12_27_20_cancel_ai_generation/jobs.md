### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de cancelación por Escape o clic fuera; lo leído ai trace escape es una función de escape de strings JSON, no un handler de eventos de UI
Abierto:
- handle route
- json escape

### Trabajo 2
consulta: dónde se limpia el archivo o se deshace la inserción parcial al cancelar la IA
keep: M8
leído: cancel current, clear pending insert, cancel level1, cancel all, handle user input, handle route, is cancel input, run insert async
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, amplia un hop; no refutes el claim entero.
encontré el mecanismo de limpieza de inserción parcial al cancelar: es clear pending insert llamado por cancel current o directamente desde handle user input que resetea el flag pending insert y el ancla insert anchor . La cancelación por Escape clic fuera no está en este código solo hay cancel textual, pero la limpieza de la inserción pendiente sí está anclada aquí.
Abierto:
- handler escape key
- handler click outside


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel current, clear pending insert, cancel level1, cancel all
    extra: Ai Controller, Symbol Filter Runner

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → Ai Controller
  T2 → Symbol Filter Runner
