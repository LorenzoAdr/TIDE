### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar errores
keep: M6
leído: visual highlight cpp, diagnostic severity tr, diagnostics for editor line, Make Editor Panel
Cerrado:
encontré el objeto de la consulta: la línea roja subrayado se dibuja en Make Editor Panel dentro del bucle for . El mecanismo es underline diagnostics que se asigna a line diagnostics si rich session enabled es true. Este puntero se pasa al renderizado de la línea implícito en el contexto de Make Editor Panel donde se usa para aplicar la decoración de subrayado rojo. El marcador en el gutter W es independiente y se gestiona con gutter marker y suffix ptr .
Abierto:
- render line with decorations

### Trabajo 2
consulta: dónde se extraen las ubicaciones de los errores del texto de la consola para generar diagnósticos
keep: M2
leído: diagnostics for translation unit, filter diagnostics by paths, parse gcc style, parse msvc style, parse trailing location, find compiler location, terminal row link
Cerrado:
encontré el mecanismo: la extracción de ubicaciones de errores del texto de la consola ocurre en find compiler location que delega en parse gcc style parse msvc style y parse trailing location . El disparo desde la consola se realiza en terminal row link que invoca a find compiler location sobre el texto de cada fila renderizada.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se envían los diagnósticos extraídos de la consola al editor para que se dibujen
keep: M1 M5
leído: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, drain visual highlight results, cached file diagnostics, apply visual highlight fold regions, diagnostics for editor line, diagnostic severity tr
Cerrado:
encontré el mecanismo: los diagnósticos se envían al editor mediante apply lsp diagnostics to panel que sincroniza el cache sync diagnostic cache y reconstruye los mapas por línea rebuild diagnostics by line . El editor lee estos mapas vía diagnostics for editor line para dibujar las marcas visuales gutter suffix .
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: visual highlight cpp, diagnostic severity tr, diagnostics for editor line, Make Editor Panel
    extra: make diagnostic modal, Make Main Layout

T2
    visto: diagnostics for translation unit, filter diagnostics by paths, parse gcc style, parse msvc style
    extra: Make Main Layout, build include tree

T3
    visto: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, drain visual highlight results
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: diagnostic severity tr
  T1=>T3  Make Editor Panel → apply lsp diagnostics to panel
  T2=>T3  apply lsp diagnostics to panel → sync diagnostic cache → diagnostics for translation unit
hacia el resto:
  T1 → make diagnostic modal
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → build include tree
  T3 → ISymbol Provider
  T3 → allows lsp ui
