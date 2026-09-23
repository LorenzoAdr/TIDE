### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M2
leído: push active file diagnostics from cache, apply lsp diagnostics to panel, collect diagnostic decorations, line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: los errores se marcan en el margen gutter mediante la función line gutter marker en editor panel que consulta line diagnostic marker from map . Esta última lee el mapa panel diagnostics by line poblado por push active file diagnostics from cache a partir de LSP y devuelve el carácter para errores o W para warnings. El renderizado final ocurre en el pipeline de UI que invoca line gutter marker para cada línea.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se analizan los errores de compilación que aparecen en la consola inferior
keep: M7
leído: problems visible terminal lines, problems tab active console, Event, document text for path, visible line count, Make Diagnostics Panel, build rows
Cerrado:
encontré el objeto de la consulta: el análisis y renderizado de los errores de compilación en la consola inferior ocurre en diagnostics panel . La función build rows llamada desde Make Diagnostics Panel y Event es el núcleo del análisis: filtra, ordena y transforma los diagnostics del LSP en filas visibles Diagnostic Row . Make Diagnostics Panel integra este análisis en el componente de la UI que se muestra en la pestaña Problems de la consola inferior.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se actualiza el mapa de diagnósticos por línea a partir de los errores de la consola
keep: M6 M8
leído: push active file diagnostics from cache, rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel
Cerrado:
encontré el objeto de la consulta: el mapa de diagnósticos por línea panel diagnostics by line se actualiza en rebuild diagnostics by line 814 834, que es invocado por rebuild diagnostics by line if changed cuando los datos de la consola cache han cambiado. La orquestación del disparo desde la consola ocurre en apply lsp diagnostics to panel que sincroniza el cache y luego empuja los datos activos.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel, collect diagnostic decorations, line gutter marker
    extra: ISymbol Provider, invalidate editor view

T2
    visto: problems visible terminal lines, problems tab active console, Event, document text for path
    extra: content box laid out, terminal height

T3
    visto: push active file diagnostics from cache, rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  extra toca visto: build rows
  T1=>T3  mismo objeto: push active file diagnostics from cache
  T2=>T3  extra toca visto: build rows
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → content box laid out
  T2 → terminal height
  T3 → ISymbol Provider
  T3 → allows lsp ui
