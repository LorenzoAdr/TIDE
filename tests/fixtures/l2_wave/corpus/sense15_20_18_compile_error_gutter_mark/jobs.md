### Trabajo 1
consulta: dónde se marcan los errores de compilación con una línea roja en el margen del editor
keep: M1 M4
leído: line diagnostic marker, line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo de marca: line gutter marker delega a line diagnostic marker from map que consulta diagnostics for editor line y retorna para errores k Error o W para warnings. El carácter se usa en el gutter. No he leído el código que pinta el color rojo asociado a ese carácter renderizado del gutter, pero la lógica de decisión está completa.
Abierto:
- gutter render color logic

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola para generar diagnósticos del editor
keep: M1 M3
leído: line diagnostic marker, line gutter marker, line diagnostic marker from map
Cerrado:
leído: line diagnostic marker, line gutter marker, line diagnostic marker from map
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos del editor
keep: M2 M6
leído: diagnostics cpp, editor panel cpp, apply lsp diagnostics to panel, sync diagnostic cache
Cerrado:
leído: diagnostics cpp, editor panel cpp, apply lsp diagnostics to panel, sync diagnostic cache
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line diagnostic marker, line gutter marker, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: line diagnostic marker, line gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T3
    visto: diagnostics cpp, editor panel cpp, apply lsp diagnostics to panel, sync diagnostic cache
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  mismo objeto: line diagnostic marker
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → ISymbol Provider
  T3 → allows lsp ui
