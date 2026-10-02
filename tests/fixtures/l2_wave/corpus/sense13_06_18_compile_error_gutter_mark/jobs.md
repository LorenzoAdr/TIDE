### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M7 M12
leído: diagnostics on line, diagnostic severity tr, line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: los errores se marcan en el gutter mediante el carácter devuelto por line diagnostic marker from map que consulta diagnostics for editor line el cual es usado por line gutter marker para decidir qué pintar en el margen.
Abierto:
- handle editor mouse
- render gutter

### Trabajo 2
consulta: dónde se procesan los errores de compilación que aparecen en la consola inferior para extraer su línea y mensaje
keep: M8
leído: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console, console panel tab active
Cerrado:
leído: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console, console panel tab active
Abierto:
- handle editor mouse
- handle gutter marker click
- handle editor chrome mouse

### Trabajo 3
consulta: dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola inferior
keep: M9
leído: diagnostics cpp, symbol provider hpp
Cerrado:
leído: diagnostics cpp, symbol provider hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics on line, diagnostic severity tr, line gutter marker, handle gutter marker click
    extra: handle editor mouse, Make Editor Panel

T2
    visto: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console
    extra: handle gutter marker click, handle editor mouse

T3
    visto: diagnostics cpp, symbol provider hpp

entre abiertas:
  T1=>T2  mismo objeto: diagnostics on line
  T1=>T2  handle problems button click → make diagnostic modal → diagnostic severity tr
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → Make Editor Panel
  T2 → handle editor mouse
  T2 → Make Main Layout
