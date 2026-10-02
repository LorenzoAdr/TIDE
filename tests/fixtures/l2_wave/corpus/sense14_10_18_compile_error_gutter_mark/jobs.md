### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
keep: M1 M2
leído: line gutter marker, handle gutter marker click, Event
Cerrado:
leído: line gutter marker, handle gutter marker click, Event
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1 M12
leído: make terminal panel
Cerrado:
leído: make terminal panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
keep: M1 M2
leído: line gutter marker, handle gutter marker click, line diagnostic marker from map, git line changed, diagnostics for editor line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de decisión del marcador la línea roja es el carácter devuelto por line gutter marker, pero no encontré la función que ejecuta la pintura gráfica en el margen el efecto visual . Lo leído son las funciones de consulta de datos y decisión de carácter.
Abierto:
- handle editor mouse
- paint gutter
- draw gutter
- render gutter


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, handle gutter marker click, Event
    extra: Make Editor Panel, Make Main Layout

T2
    visto: make terminal panel
    extra: Code Bg, Panel Title

T3
    visto: line gutter marker, handle gutter marker click, line diagnostic marker from map, git line changed
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Code Bg
  T2 → Panel Title
  T3 → Make Editor Panel
  T3 → Make Main Layout
