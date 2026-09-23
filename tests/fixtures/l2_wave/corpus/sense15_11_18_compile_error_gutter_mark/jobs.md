### Trabajo 1
consulta: dónde se aplica la decoración de error en el margen del editor
keep: M1 M5
leído: collect diagnostic decorations, apply decoration, Render Editor Line, render rich line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la aplicación de la decoración en el margen gutter; lo leído muestra que la decoración de error se aplica sobre el cuerpo del texto celdas de la línea mediante apply decoration con color no en el margen
Abierto:
- gutter renderer
- make gutter column
- diagnostic gutter marker

### Trabajo 2
consulta: dónde se renderiza la columna del margen izquierdo del editor y cómo se añaden marcadores o decoraciones a esa zona
keep: M1 M8
leído: line gutter marker, gutter buffer line at row, Make Editor Panel, line diagnostic marker from map, git line changed, Render Editor Line
Cerrado:
leído: line gutter marker, gutter buffer line at row, Make Editor Panel, line diagnostic marker from map, git line changed, Render Editor Line
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se crea el marcador de diagnóstico en el margen del editor a partir de un mapa de errores
keep: M1 M2
leído: apply lsp diagnostics to panel, line diagnostic marker from map, line diagnostic marker, line gutter marker
Cerrado:
encontré el objeto: el marcador en el margen se crea en line gutter marker 1005, que delega en line diagnostic marker from map para obtener el carácter o W a partir del mapa de errores panel diagnostics by line .
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: collect diagnostic decorations, apply decoration, Render Editor Line, render rich line
    extra: Make Editor Panel, make sticky overlay

T2
    visto: line gutter marker, gutter buffer line at row, Make Editor Panel, line diagnostic marker from map
    extra: Make Main Layout, handle editor mouse

T3
    visto: apply lsp diagnostics to panel, line diagnostic marker from map, line diagnostic marker, line gutter marker
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  mismo objeto: Render Editor Line
  T1=>T2  Make Editor Panel → Render Editor Line → render rich line → collect diagnostic decorations
  T1=>T3  sin camino
  T2=>T3  mismo objeto: line gutter marker
  T2=>T3  Make Editor Panel → apply lsp diagnostics to panel
hacia el resto:
  T1 → make sticky overlay
  T1 → Bracket Match Bg
  T2 → Make Main Layout
  T2 → handle editor mouse
  T3 → ISymbol Provider
  T3 → allows lsp ui
