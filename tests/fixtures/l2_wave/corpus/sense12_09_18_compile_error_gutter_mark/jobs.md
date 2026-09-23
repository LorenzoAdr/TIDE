### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola para extraer la línea del error
keep: M7
leído: console panel cpp
Cerrado:
leído: console panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: line gutter marker, handle gutter marker click, handle git gutter click, line diagnostic marker from map, git line changed, Make Editor Panel
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la función que dibuja la línea roja; lo leído son funciones que devuelven marcadores de texto W, G para el gutter, pero no el código de renderizado paint draw que pinta el color rojo en el margen
Abierto:
- handle editor mouse
- paint gutter
- draw line marker

### Trabajo 3
consulta: dónde se pinta el color de fondo del marcador en el margen izquierdo del editor
keep: M1
leído: line gutter marker
Cerrado:
leído: line gutter marker
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se extrae la línea del error del texto de la consola
keep: M7
leído: terminal selected text, select console line at
Cerrado:
leído: terminal selected text, select console line at
Abierto:
- handle console panel mouse


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: console panel cpp

T2
    visto: line gutter marker, handle gutter marker click, handle git gutter click, line diagnostic marker from map
    extra: Make Main Layout, handle editor mouse

T3
    visto: line gutter marker
    extra: Make Editor Panel, Make Main Layout

T4
    visto: terminal selected text, select console line at
    extra: copy terminal selection, forward pty key

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: line gutter marker
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T2 → Make Main Layout
  T2 → handle editor mouse
  T3 → Make Main Layout
  T4 → copy terminal selection
  T4 → forward pty key
