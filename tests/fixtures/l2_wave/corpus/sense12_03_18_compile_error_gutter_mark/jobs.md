### Trabajo 1
consulta: dónde se extraen la línea y columna de los errores de compilación del texto de la consola
keep: M11
leído: diagnostics for editor line
Cerrado:
leído: diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M1
leído: diagnostics for editor line, line gutter marker, gutter buffer line at row, line diagnostic marker from map, Make Editor Panel
Cerrado:
leído: diagnostics for editor line, line gutter marker, gutter buffer line at row, line diagnostic marker from map, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, line gutter marker, gutter buffer line at row, line diagnostic marker from map
    extra: Make Main Layout, git line changed

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
hacia el resto:
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → git line changed
