### Trabajo 1
consulta: dónde se pinta la línea roja en el margen del editor para marcar un error
keep: M1 M4
leído: line gutter marker, fold gutter marker, line diagnostic marker from map
Cerrado:
encontré el mecanismo: la línea roja marcador se determina en line diagnostic marker from map 978 que retorna si hay un diagnóstico con k Error . Este carácter es devuelto por line gutter marker y pintado en el margen por el sistema de renderizado del editor paint caches .
Abierto:
- handle fold gutter click
- paint gutter line

### Trabajo 2
consulta: dónde se escanea la salida de la consola para extraer errores de compilación y convertirlos en diagnósticos
keep: M1 M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se escanea la salida de la consola para extraer errores de compilación y convertirlos en diagnósticos
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se convierten los errores de compilación de la consola en diagnósticos del editor
keep: M2 M6
leído: sync diagnostic cache, count workspace diagnostics, apply lsp diagnostics to panel, line diagnostic marker from map, diagnostics for editor line, push active file diagnostics from cache, cached file diagnostics, line gutter marker
Cerrado:
encontré el objeto de la consulta: la conversión ocurre en editor panel cpp El flujo es: 1 sync diagnostic cache obtiene los datos crudos de LSP vía symbols- diagnostics for file path y los almacena en panel- cached file diag 2 apply lsp diagnostics to panel invoca push active file diagnostics from cache, que itera sobre esos datos y los indexa en panel- diagnostics by line mapa línea - vector Diagnostic . 3 line gutter marker consulta ese mapa vía line diagnostic marker from map, que inspecciona la severidad kError kWarning y devuelve el carácter visual o W . Los errores de consola son en realidad diagnósticos LSP que se transforman en marcadores de gutter en este punto.
Abierto:
- symbols diagnostics for file


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, fold gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

T3
    visto: sync diagnostic cache, count workspace diagnostics, apply lsp diagnostics to panel, line diagnostic marker from map
    extra: ISymbol Provider, snapshot

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T3 → ISymbol Provider
  T3 → snapshot
