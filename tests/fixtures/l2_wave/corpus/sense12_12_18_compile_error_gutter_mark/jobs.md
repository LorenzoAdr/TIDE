### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Cerrado:
leído: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1
leído: diagnostics for editor line, diagnostics on line, line diagnostic marker from map
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la lectura de errores de compilación de la consola inferior; lo leído es el renderizado de diagnostics LSP en el gutter del editor
Abierto:
- handle editor mouse
- handle gutter marker click
- parse compilation errors
- on output


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, diagnostics on line, line diagnostic marker from map
    extra: Make Main Layout, Make Editor Panel

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → Make Editor Panel
