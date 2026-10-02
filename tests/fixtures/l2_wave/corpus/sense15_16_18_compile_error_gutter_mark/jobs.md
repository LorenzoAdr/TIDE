### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1 M5
leído: diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: los errores se marcan en line diagnostic marker from map editor panel cpp, que consulta diagnostics for editor line y retorna si la severidad es k Error este carácter se usa en line gutter marker para dibujar el icono en el margen.
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la salida de la consola
keep: M2 M11
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se convierten los errores leídos de la consola en diagnósticos para el editor
keep: M1 M6
leído: parse diagnostic, diagnostics for file, parse publish diagnostics, on lsp notification
Cerrado:
encontré el mecanismo de conversión: los errores LSP se convierten en diagnósticos en parse diagnostic parseo JSON a struct Diagnostic y se agrupan en parse publish diagnostics almacenándose en diagnostics desde lsp notification . El editor los consume vía diagnostics for file y los aplica en apply lsp diagnostics to panel .
Abierto:
- apply lsp diagnostics to panel


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map
    extra: handle editor mouse, Make Editor Panel

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T3
    visto: parse diagnostic, diagnostics for file, parse publish diagnostics, on lsp notification
    extra: contains, is number integer

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → Make Editor Panel
  T2 → tick binary symbols panel
  T2 → has value
  T3 → contains
  T3 → is number integer
