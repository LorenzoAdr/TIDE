### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Cerrado:
encontré dónde se marcan los errores de compilación en el margen: en line diagnostic marker from map que itera los diagnostics de la línea y devuelve si hay un k Error este carácter es devuelto por line gutter marker y usado para dibujar el marcador en el gutter.
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de lectura de errores de compilación en la consola inferior; lo leído es un parser de errores de linker en el panel de símbolos binarios, no en la consola
Abierto:
- handle click on error
- parse compilation errors


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click
    extra: Make Editor Panel, Make Main Layout

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → tick binary symbols panel
  T2 → has value
