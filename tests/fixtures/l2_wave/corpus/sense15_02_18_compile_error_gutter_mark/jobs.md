### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M8 M2
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Cerrado:
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la salida de la consola
keep: M3 M7
leído: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
Cerrado:
leído: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la salida de la consola
keep: M8 M11
leído: parse pty filter tokens, problem frame wants anchor hypotheses
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea errores de compilación de la salida de la consola; lo leído son utilidades de filtrado de tokens de la terminal parse pty filter tokens y lógica de confianza de anclas problem frame wants anchor hypotheses, que no procesan diagnósticos del compilador
Abierto:
- parse compiler output
- diagnostic parser
- build log parser


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel
    extra: ISymbol Provider, invalidate editor view

T2
    visto: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
    extra: build console panel view, render app terminal

T3
    visto: parse pty filter tokens, problem frame wants anchor hypotheses
    extra: build console panel view, render app terminal

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: parse pty filter tokens
  T2=>T3  problem frame wants anchor hypotheses → ascii lower
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → build console panel view
  T2 → render app terminal
  T3 → build console panel view
  T3 → render app terminal
