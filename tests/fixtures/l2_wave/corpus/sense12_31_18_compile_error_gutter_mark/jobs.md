### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error de compilación
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker
Cerrado:
leído: diagnostics for editor line, diagnostics on line, line gutter marker
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para crear marcadores en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, line diagnostic marker from map
Cerrado:
encontré el mecanismo: los marcadores se crean en line gutter marker editor panel cpp, que lee los diagnostics de panel diagnostics by line vía diagnostics for editor line y decide el carácter o W en line diagnostic marker from map
Abierto:
- handle editor mouse
- handle gutter marker click
- diagnostics by line update

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola inferior para alimentar los diagnostics
keep: M11
leído: diagnostics for editor line, diagnostics on line
Cerrado:
leído: diagnostics for editor line, diagnostics on line
Abierto:
- handle editor mouse
- handle gutter marker click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T3
    visto: diagnostics for editor line, diagnostics on line
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
  T1=>T3  mismo objeto: diagnostics for editor line
  T2=>T3  mismo objeto: diagnostics for editor line
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → Make Editor Panel
  T3 → Make Main Layout
