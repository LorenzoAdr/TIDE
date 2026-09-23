### Trabajo 1
consulta: dónde se pintan las marcas rojas en el margen izquierdo del editor
keep: M1 M3
leído: apply myers git marks to panel, apply visual highlight git marks, apply line diff result to panel
Cerrado:
leído: apply myers git marks to panel, apply visual highlight git marks, apply line diff result to panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se construye el snapshot de marcas para el margen del editor
keep: M3
leído: build git marks snapshot
Cerrado:
encontré el objeto de la consulta: el snapshot de marcas se construye en build git marks snapshot que calcula el diff de líneas y llena snap overview git changed lines y snap overview git previous by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para añadirlos al snapshot de marcas
keep: M1 M3
leído: build git marks snapshot
Cerrado:
leído: build git marks snapshot
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se leen los errores de compilación de la consola para añadirlos al snapshot de marcas
keep: M1 M7
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se leen los errores de compilación para añadirlos al snapshot de marcas; lo leído scan shell output for linker errors solo parsea referencias indefinidas de linker para el panel de símbolos binarios, no errores de compilación para marcas visuales
Abierto:
- parse compilation errors
- extract errors from console
- build compilation marks snapshot


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: apply myers git marks to panel, apply visual highlight git marks, apply line diff result to panel
    extra: Make Editor Panel, sync git cache

T2
    visto: build git marks snapshot
    extra: worker main, Visual Highlight Service

T3
    visto: build git marks snapshot
    extra: compute line diff, sync git cache

T4
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  extra toca visto: build git marks snapshot
  T1=>T3  extra toca visto: build git marks snapshot
  T1=>T4  sin camino
  T2=>T3  mismo objeto: build git marks snapshot
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → sync git cache
  T2 → worker main
  T2 → Visual Highlight Service
  T3 → compute line diff
  T3 → sync git cache
