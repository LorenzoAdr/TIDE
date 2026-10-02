### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1 M11
leído: line gutter marker, line diagnostic marker from map, git line changed, Make Editor Panel
Cerrado:
leído: line gutter marker, line diagnostic marker from map, git line changed, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcar la línea en el editor
keep: M5 M8
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
encontré el mecanismo: los errores diagnósticos LSP se leen del cache en push active file diagnostics from cache que llama a cached file diagnostics se agrupan por línea en rebuild diagnostics by line iterando doc items y usando item line y se invalida la vista del editor con invalidate editor view para que se pinten las marcas.
Abierto:
- cached file diagnostics
- invalidate editor view

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para marcar la línea en el editor
keep: M1 M5
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, parse gfortran stderr, refresh fortran compiler diagnostics, run gfortran diagnostics
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: no se leen los errores de la consola UI directamente; se ejecuta gfortran en segundo plano run gfortran diagnostics se captura su stderr run shell capture se parsea parse gfortran stderr y se inyecta en el sistema de diagnósticos refresh fortran compiler diagnostics - notify - apply lsp diagnostics to panel - rebuild diagnostics by line . La consola es una salida de proceso, no un widget de terminal leído por el editor.
Abierto:
- run shell capture


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map, git line changed, Make Editor Panel
    extra: Make Main Layout, diagnostics for editor line

T2
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
    extra: ISymbol Provider, panel diagnostics match doc

T3
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, parse gfortran stderr
    extra: push active file diagnostics from cache, ISymbol Provider

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: rebuild diagnostics by line if changed
hacia el resto:
  T1 → Make Main Layout
  T1 → diagnostics for editor line
  T2 → ISymbol Provider
  T2 → panel diagnostics match doc
  T3 → ISymbol Provider
  T3 → panel diagnostics match doc
