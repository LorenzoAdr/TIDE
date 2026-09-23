### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: mark visual highlight inputs dirty, lsp sync pending
Cerrado:
leído: mark visual highlight inputs dirty, lsp sync pending
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación del texto de la consola para marcarlos
keep: M4
leído: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link, find compiler location
Cerrado:
leído: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link, find compiler location
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los errores leídos de la consola al sistema de resaltado visual del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel, visual highlight config from settings, compute
Cerrado:
leído: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel, visual highlight config from settings, compute
Abierto:
- handle editor mouse


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: mark visual highlight inputs dirty, lsp sync pending
    extra: sync git cache, Make Editor Panel

T2
    visto: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link
    extra: parse positive int, pop back

T3
    visto: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: problems visible terminal lines
  T2=>T3  Make Console Panel → handle console panel mouse → terminal link at cell → terminal row link → find compiler location → parse gcc style
hacia el resto:
  T1 → sync git cache
  T1 → Make Editor Panel
  T2 → parse positive int
  T2 → pop back
  T3 → Make Editor Panel
  T3 → Make Main Layout
