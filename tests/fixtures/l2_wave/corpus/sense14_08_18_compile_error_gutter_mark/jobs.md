### Trabajo 1
consulta: dónde se pinta la línea roja en el margen del editor para marcar un error
keep: M1 M5
leído: line gutter marker, line diagnostic marker from map
Cerrado:
encontré el mecanismo: line gutter marker devuelve si line diagnostic marker from map detecta un k Error; el pintado visual color rojo se delega al renderizado del gutter que consume ese carácter, pero la lógica de decisión es esta cadena
Abierto:
- gutter render impl
- paint gutter char

### Trabajo 2
consulta: dónde se extraen los errores de compilación de la consola inferior para añadirlos al mapa de diagnósticos del editor
keep: M1 M3
leído: rebuild diagnostics by line if changed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la extracción de errores de compilación de la consola inferior; lo leído es la integración de diagnósticos LSP apply lsp diagnostics to panel y la reconstrucción del mapa por línea, que opera sobre datos ya estructurados, no sobre texto de consola
Abierto:
- parse console output
- extract errors from text
- console to diagnostics

### Trabajo 3
consulta: dónde se analiza el texto de la consola inferior para detectar errores de compilación
keep: M10 M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se analiza el texto de la consola inferior para detectar errores de compilación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T2
    visto: rebuild diagnostics by line if changed
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → apply lsp diagnostics to panel
  T2 → push active file diagnostics from cache
