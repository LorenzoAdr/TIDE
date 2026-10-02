### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M2
leído: push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: line gutter marker decide el carácter o W consultando line diagnostic marker from map que a su vez lee diagnostics for editor line del mapa panel diagnostics by line este mapa se llena en push active file diagnostics from cache a partir de los diagnostics del LSP.
Abierto:
- render gutter

### Trabajo 2
consulta: dónde se leen los errores de compilación de la salida de la consola para marcarlos en el editor
keep: M1
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Cerrado:
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsea la salida de la consola para extraer errores de compilación
keep: M11
leído: parse pty filter tokens, on pty output
Cerrado:
leído: parse pty filter tokens, on pty output
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker, gutter buffer line at row
    extra: ISymbol Provider, invalidate editor view

T2
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel
    extra: ISymbol Provider, invalidate editor view

T3
    visto: parse pty filter tokens, on pty output
    extra: render app terminal, build console panel view

entre abiertas:
  T1=>T2  mismo objeto: push active file diagnostics from cache
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → ISymbol Provider
  T2 → invalidate editor view
  T3 → render app terminal
  T3 → build console panel view
