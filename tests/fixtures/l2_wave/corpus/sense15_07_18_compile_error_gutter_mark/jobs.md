### Trabajo 1
consulta: dónde se detectan los errores de compilación en la consola
keep: M3 M10
leído: count workspace diagnostics, Make Diagnostics Panel, count errors, build rows, Make Console Panel, diagnostics display allowed
Cerrado:
encontré el objeto de la consulta: la detección de errores de compilación en la consola se realiza en build rows que ingesta los diagnósticos del LSP vía ISymbol Provider y los filtra por severidad k Error lógica en count errors siendo luego renderizados por Make Diagnostics Panel dentro de Make Console Panel
Abierto:
- diagnostics for translation unit

### Trabajo 2
consulta: dónde se dibujan las marcas visuales en el margen del editor
keep: M1 M7
leído: collect diagnostic decorations, apply decoration, render rich line, line gutter marker, fold gutter marker
Cerrado:
leído: collect diagnostic decorations, apply decoration, render rich line, line gutter marker, fold gutter marker
Abierto:
- handle fold gutter click

### Trabajo 3
consulta: dónde se envían los diagnósticos de compilación desde la consola al editor
keep: M1 M3
leído: parse diagnostic, parse publish diagnostics, on lsp notification, set diagnostics notify callback, Application
Cerrado:
leído: parse diagnostic, parse publish diagnostics, on lsp notification, set diagnostics notify callback, Application
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: count workspace diagnostics, Make Diagnostics Panel, count errors, build rows
    extra: Make Main Layout, count warnings

T2
    visto: collect diagnostic decorations, apply decoration, render rich line, line gutter marker
    extra: Make Editor Panel, make sticky overlay

T3
    visto: parse diagnostic, parse publish diagnostics, on lsp notification, set diagnostics notify callback
    extra: contains, is number integer

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → count warnings
  T2 → Make Editor Panel
  T2 → make sticky overlay
  T3 → contains
  T3 → is number integer
