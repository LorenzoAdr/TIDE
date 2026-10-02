### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M12
leído: parse pty filter tokens
Cerrado:
leído: parse pty filter tokens
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1 M6
leído: decoration at, apply decoration, Render Editor Line, line gutter marker, gutter buffer line at row
Cerrado:
leído: decoration at, apply decoration, Render Editor Line, line gutter marker, gutter buffer line at row
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se conecta la detección de errores de compilación con la aplicación de decoraciones en el editor
keep: M1 M12
leído: apply lsp diagnostics to panel, push active file diagnostics from cache, cached file diagnostics, sync diagnostic cache, diagnostics revision, supports diagnostics, client diagnostics revision
Cerrado:
encontré el puente: la detección de errores LSP se conecta con las decoraciones del editor a través de la API ISymbol Provider símbolos . El mecanismo es una revisión incremental: apply lsp diagnostics to panel editor consulta symbols diagnostics revision si cambia, invoca sync diagnostic cache que lee los diagnostics del proveedor LSP lsp symbol provider y luego push active file diagnostics from cache aplica los subrayados y sufijos al panel.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: parse pty filter tokens
    extra: build console panel view, render app terminal

T2
    visto: decoration at, apply decoration, Render Editor Line, line gutter marker
    extra: Make Editor Panel, make sticky overlay

T3
    visto: apply lsp diagnostics to panel, push active file diagnostics from cache, cached file diagnostics, sync diagnostic cache
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → build console panel view
  T1 → render app terminal
  T2 → Make Editor Panel
  T2 → make sticky overlay
  T3 → ISymbol Provider
  T3 → allows lsp ui
