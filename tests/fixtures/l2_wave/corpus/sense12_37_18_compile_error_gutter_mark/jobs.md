### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: editor panel cpp, editor panel hpp
Cerrado:
leído: editor panel cpp, editor panel hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1 M11
leído: editor panel hpp, editor panel cpp, diagnostic severity tr, diagnostics on line, scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola y los marca en el editor; lo leído es un escáner de errores de linker para símbolos binarios binary symbols panel y un sistema de diagnóstico LSP que consume datos ya estructurados, no texto de consola
Abierto:
- handle editor mouse
- console to diagnostics
- parse compiler output
- rebuild display

### Trabajo 3
consulta: dónde se parsea la salida de la consola para extraer errores de compilación
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsea la salida de la consola para extraer errores de compilación
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor panel cpp, editor panel hpp

T2
    visto: editor panel hpp, editor panel cpp, diagnostic severity tr, diagnostics on line
    extra: make diagnostic modal, Make Editor Panel

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: editor panel cpp
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → make diagnostic modal
  T2 → Make Editor Panel
