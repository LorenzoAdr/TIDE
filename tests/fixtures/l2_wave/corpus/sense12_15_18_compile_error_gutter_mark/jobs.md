### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Cerrado:
encontré dónde se marcan los errores: en line diagnostic marker from map editor panel cpp que devuelve para errores y W para warnings, usado por line gutter marker para pintar el carácter en el margen
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors, Event
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo, o ampliar otro polo de existencia (no el mismo disparo).
no encontré la lógica que lee parsea errores de compilación de la consola inferior; lo leído es UI de selección de texto en la consola y parsing de errores de linker en un panel de símbolos binarios que no es el objeto de la consulta
Abierto:
- parse compile errors from console
- console error parser


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click
    extra: Make Editor Panel, Make Main Layout

T2
    visto: scan shell output for linker errors, Event
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → tick binary symbols panel
  T2 → has value
