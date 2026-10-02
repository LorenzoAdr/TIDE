### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M2
leído: rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se convierten los errores de la consola en diagnósticos para el editor
keep: M1
leído: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache
Cerrado:
encontré el objeto de la consulta: la conversión ocurre en editor panel cpp, donde apply lsp diagnostics to panel orquesta la lectura de diagnósticos LSP vía sync diagnostic cache que llama a symbols- diagnostics for file y su proyección visual vía push active file diagnostics from cache que llena diagnostics by line e invalida la vista .
Abierto:
- diagnostics for file
- diagnostics reveal allowed

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para crear diagnósticos
keep: M4
leído: rebuild diagnostics by line, apply lsp diagnostics to panel, sync diagnostic cache, parse gfortran stderr, run gfortran diagnostics
Cerrado:
encontré el mecanismo: los errores de compilación no se leen de la consola UI, sino que se capturan del stderr del compilador gfortran vía run shell capture en run gfortran diagnostics y se parsean en parse gfortran stderr para crear diagnósticos.
Abierto:
- run shell capture


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line
    extra: rebuild diagnostics by line if changed, apply lsp diagnostics to panel

T2
    visto: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache
    extra: ISymbol Provider, allows lsp ui

T3
    visto: rebuild diagnostics by line, apply lsp diagnostics to panel, sync diagnostic cache, parse gfortran stderr
    extra: rebuild diagnostics by line if changed, push active file diagnostics from cache

entre abiertas:
  T1=>T2  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
  T1=>T3  mismo objeto: rebuild diagnostics by line
  T2=>T3  mismo objeto: apply lsp diagnostics to panel
  T2=>T3  push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
hacia el resto:
  T1 → rebuild diagnostics by line if changed
  T1 → ISymbol Provider
  T2 → ISymbol Provider
  T2 → allows lsp ui
  T3 → rebuild diagnostics by line if changed
  T3 → ISymbol Provider
