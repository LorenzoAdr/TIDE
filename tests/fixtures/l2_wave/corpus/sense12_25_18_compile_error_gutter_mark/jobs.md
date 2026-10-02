### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen izquierdo del editor
keep: M1
leído: diagnostics for editor line, line gutter marker, diagnostics on line, line diagnostic marker from map
Cerrado:
encontré el mecanismo: los errores de compilación se marcan en el margen izquierdo mediante la función line gutter marker en editor panel que consulta line diagnostic marker from map esta última itera sobre los diagnostics de la línea obtenidos vía diagnostics for editor line y devuelve el carácter si encuentra un k Error el cual es renderizado en el gutter.
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para añadirlos a los diagnostics del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line
Cerrado:
leído: diagnostics for editor line, diagnostics on line
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola inferior para añadirlos a los diagnostics del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line
Cerrado:
leído: diagnostics for editor line, diagnostics on line
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 4
consulta: dónde se leen los errores de compilación de la consola inferior para añadirlos a los diagnostics del editor
keep: M1 M7
leído: diagnostics for editor line, diagnostics on line, line diagnostic marker from map, scan shell output for linker errors
Cerrado:
leído: diagnostics for editor line, diagnostics on line, line diagnostic marker from map, scan shell output for linker errors
Abierto:
- handle editor mouse
- handle gutter marker click


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, line gutter marker, diagnostics on line, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, diagnostics on line
    extra: Make Editor Panel, Make Main Layout

T3
    visto: diagnostics for editor line, diagnostics on line
    extra: Make Editor Panel, Make Main Layout

T4
    visto: diagnostics for editor line, diagnostics on line, line diagnostic marker from map, scan shell output for linker errors
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
  T1=>T3  mismo objeto: diagnostics for editor line
  T1=>T4  mismo objeto: diagnostics for editor line
  T2=>T3  mismo objeto: diagnostics for editor line
  T2=>T4  mismo objeto: diagnostics for editor line
  T3=>T4  mismo objeto: diagnostics for editor line
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → Make Editor Panel
  T3 → Make Main Layout
