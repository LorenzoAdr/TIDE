### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1 M12
leído: push active file diagnostics from cache, line gutter marker, apply lsp diagnostics to panel, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: line gutter marker 1005 decide el carácter del margen para error, W para warning consultando line diagnostic marker from map, que a su vez usa diagnostics for editor line para leer el mapa panel- diagnostics by line Este mapa se llena en push active file diagnostics from cache 1333 a partir de la caché de diagnostics del LSP.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola inferior
keep: M9 M12
leído: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
Cerrado:
leído: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para generar diagnostics
keep: M12 M2
leído: parse diagnostic
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el parsing de errores de compilación desde texto de consola; lo leído es el parsing de JSON estructurado de LSP parse diagnostic, que no procesa texto plano de stdout stderr
Abierto:
- compiler error parser
- parse compile errors


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, line gutter marker, apply lsp diagnostics to panel, line diagnostic marker from map
    extra: ISymbol Provider, invalidate editor view

T2
    visto: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
    extra: Make Main Layout, clamp scroll viewport

T3
    visto: parse diagnostic
    extra: on lsp notification, parse publish diagnostics

entre abiertas:
  T1=>T2  extra toca visto: build rows
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → Make Main Layout
  T2 → clamp scroll viewport
  T3 → on lsp notification
  T3 → parse publish diagnostics
