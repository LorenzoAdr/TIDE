### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M8 M1
leído: push active file diagnostics from cache, apply lsp diagnostics to panel, rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line
Cerrado:
leído: push active file diagnostics from cache, apply lsp diagnostics to panel, rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para marcarlos en el editor
keep: M1 M7
leído: buffer text, make problem row element
Cerrado:
leído: buffer text, make problem row element
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola para enviarlos al sistema de diagnósticos
keep: M2 M3
leído: parse gcc style, parse msvc style, find compiler location, terminal row link
Cerrado:
encontré el mecanismo de extracción: terminal row link en console panel invoca a find compiler location en compiler location que parsea estilos GCC MSVC y devuelve un Compiler Location Match ruta, línea, columna . Este objeto se usa para navegación links, no se envía directamente al sistema de diagnósticos LSP, pero es la extracción de errores de compilación desde la consola.
Abierto:
- sistema de diagnósticos LSP


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel, rebuild diagnostics by line if changed, line gutter marker
    extra: ISymbol Provider, invalidate editor view

T2
    visto: buffer text, make problem row element
    extra: Make Main Layout, editor buffer joined source

T3
    visto: parse gcc style, parse msvc style, find compiler location, terminal row link
    extra: parse positive int, pop back

entre abiertas:
  T1=>T2  push active file diagnostics from cache → cached file diagnostics → buffer text
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → Make Main Layout
  T2 → editor buffer joined source
  T3 → parse positive int
  T3 → pop back
