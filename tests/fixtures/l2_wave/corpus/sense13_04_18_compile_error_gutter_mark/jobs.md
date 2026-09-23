### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo de decisión: la línea roja se pinta cuando line gutter marker retorna lo cual ocurre si line diagnostic marker from map detecta un diagnóstico con k Error en diagnostics for editor line . El sistema es editor panel el verbo es line gutter marker y el efecto visual pintura se dispara en el renderizado del gutter no leído, pero anclado en el circuito como Make Editor Panel - line gutter marker .
Abierto:
- render gutter line

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crean los diagnósticos de error que llegan al editor para pintar la línea roja
keep: M1 M3
leído: rebuild diagnostics by line if changed, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line, apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, line gutter marker
Cerrado:
encontré el objeto de la consulta: los diagnósticos se crean obtienen en sync diagnostic cache y cached file diagnostics llamando a symbols diagnostics for file fuente LSP, se indexan por línea en rebuild diagnostics by line y se pintan en el gutter vía line gutter marker - line diagnostic marker from map que devuelve para errores.
Abierto:
- diagnostics for file


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

T3
    visto: rebuild diagnostics by line if changed, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line
    extra: ISymbol Provider, panel diagnostics match doc

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T3 → ISymbol Provider
  T3 → panel diagnostics match doc
