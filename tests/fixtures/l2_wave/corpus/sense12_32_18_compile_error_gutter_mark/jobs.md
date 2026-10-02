### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se dibuja la línea roja en el margen izquierdo del editor
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se marca la línea roja en el margen izquierdo del editor
keep: M1 M3
leído: clear editor line paint caches, build git marks snapshot
Cerrado:
leído: clear editor line paint caches, build git marks snapshot
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se extrae la línea del error del texto de la consola
keep: M11
leído: scan shell output for linker errors, parse linker undefined reference
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de extracción: en scan shell output for linker errors se itera línea a línea con getline y se delega a parse linker undefined reference para extraer el símbolo; sin embargo, el cuerpo de parse linker undefined reference no está leído el peek devolvió el header truncado, por lo que no puedo explicar la lógica de parsing exacta.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: (nada)

T3
    visto: clear editor line paint caches, build git marks snapshot
    extra: worker main, Visual Highlight Service

T4
    visto: scan shell output for linker errors, parse linker undefined reference
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  mismo objeto: scan shell output for linker errors
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T3 → worker main
  T3 → Visual Highlight Service
  T4 → tick binary symbols panel
  T4 → has value
