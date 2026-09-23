### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar errores
keep: M1
leído: line diagnostic marker, line diagnostic marker from map, line gutter marker, Make Editor Panel
Cerrado:
leído: line diagnostic marker, line diagnostic marker from map, line gutter marker, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se crean los marcadores de diagnóstico en el editor a partir de los errores de compilación
keep: M1
leído: line diagnostic marker, line diagnostic marker from map, line gutter marker, diagnostics for editor line
Cerrado:
encontré el mecanismo de creación de marcadores: line gutter marker es la función que decide el carácter visual o W para cada línea del editor, llamando a line diagnostic marker from map que a su vez consulta diagnostics for editor line para obtener los errores warnings de compilación asociados a esa línea específica desde el mapa panel diagnostics by line .
Abierto:
- update diagnostics map

### Trabajo 3
consulta: dónde se actualiza el mapa de diagnósticos por línea a partir de los errores de compilación
keep: M1
leído: line diagnostic marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
encontré el objeto de la consulta: el mapa panel diagnostics by line se actualiza en rebuild diagnostics by line 814 834, que itera sobre doc items los errores de compilación LSP y los inserta en el mapa por línea. Esta función es invocada por rebuild diagnostics by line if changed que a su vez es llamada por los disparadores apply lsp diagnostics to panel y push active file diagnostics from cache .
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line diagnostic marker, line diagnostic marker from map, line gutter marker, Make Editor Panel
    extra: Make Main Layout, diagnostics for editor line

T2
    visto: line diagnostic marker, line diagnostic marker from map, line gutter marker, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T3
    visto: line diagnostic marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line
    extra: line gutter marker, Make Editor Panel

entre abiertas:
  T1=>T2  mismo objeto: line diagnostic marker
  T1=>T2  Make Editor Panel → diagnostics for editor line
  T1=>T3  mismo objeto: line diagnostic marker
  T1=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T2=>T3  mismo objeto: line diagnostic marker
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T2 → Make Main Layout
  T2 → git line changed
  T3 → Make Main Layout
  T3 → ISymbol Provider
