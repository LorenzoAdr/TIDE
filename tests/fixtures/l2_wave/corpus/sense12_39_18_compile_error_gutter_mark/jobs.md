### Trabajo 1
consulta: dónde se marca una línea roja en el margen izquierdo del editor
keep: M1
leído: line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: la línea roja marcador se marca en line gutter marker, que delega en line diagnostic marker from map; esta función devuelve si diagnostics for editor line encuentra un diagnostic con severity k Error para esa línea.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se procesan los errores de compilación de la consola para marcarlos en el editor
keep: M1
leído: line gutter marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
encontré el objeto de la consulta: los errores se procesan en apply lsp diagnostics to panel editor panel cpp, que actúa como puente el proveedor de símbolos LSP y el panel del editor. Esta función sincroniza la caché de diagnósticos sync diagnostic cache y, si hay cambios, invoca a push active file diagnostics from cache . Esta última recupera los diagnósticos del archivo activo cached file diagnostics y llama a rebuild diagnostics by line if changed que a su vez ejecuta rebuild diagnostics by line . Esta función final itera sobre los items de Document Diagnostics y los inserta en el mapa panel diagnostics by line . Finalmente, el renderizado del editor consulta este mapa mediante diagnostics for editor line - line diagnostic marker from map
Abierto:
- sync diagnostic cache
- cached file diagnostics

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la consola para insertarlos en el mapa de diagnósticos del editor
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación de la consola para insertarlos en el mapa de diagnósticos del editor
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: line gutter marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line
    extra: Make Editor Panel, Make Main Layout

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: line gutter marker
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout
