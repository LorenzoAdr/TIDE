### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M1 M7
leído: cancel completion fetch, cancel live lsp on cursor move, handle completion keys, handle editor keys
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación al pulsar Escape se gestiona en handle completion keys 5213 llamando a completion close . No encontré evidencia de cancelación al hacer clic fuera; el sistema parece no implementar ese disparo o usa un mecanismo de foco no rastreado en los peeks actuales.
Abierto:
- click outside handler
- focus lost handler

### Trabajo 2
consulta: dónde se limpia el archivo para no dejarlo a medias tras cancelar la generación
keep: M5 M9
leído: is generation, open, cancel debug launch
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza del archivo tras cancelar la generación; lo leído es la cancelación de la sesión de debug cancel debug launch que mata el proceso pero no borra archivos temporales de generación de código
Abierto:
- debug launch generation active
- cleanup generated file

### Trabajo 3
consulta: dónde se revierte la escritura del archivo cuando se cancela la generación de código
keep: M5 M12
leído: cancel current, clear pending insert, begin insert at, cancel level1, handle user input, run insert async, cancel all
Cerrado:
leído: cancel current, clear pending insert, begin insert at, cancel level1, handle user input, run insert async, cancel all
Abierto:
- handle route
- begin thinking
- end thinking


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel completion fetch, cancel live lsp on cursor move, handle completion keys, handle editor keys
    extra: completion lsp tick, ISymbol Provider

T2
    visto: is generation, open, cancel debug launch
    extra: Tab Hover, Tab Pressed

T3
    visto: cancel current, clear pending insert, begin insert at, cancel level1
    extra: handle route, on ai tab opened
    entre interno: begin insert at → run insert async

entre abiertas:
  T1=>T2  cancel debug launch → exit debug mode → cancel live lsp on cursor move → cancel completion fetch
  T1=>T3  sin camino
  T2=>T3  clear pending insert → pending insert → run insert async → bootstrap level2 session → make l2 deps → any modal open
hacia el resto:
  T1 → completion lsp tick
  T1 → ISymbol Provider
  T2 → Tab Hover
  T2 → Tab Pressed
  T3 → handle route
  T3 → on ai tab opened
