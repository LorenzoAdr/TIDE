### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dispara el reinicio del servidor de lenguaje cuando se detecta que falta o no responde
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, clangd process alive, clangd index healthy
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el disparo para missing on lsp missing install llama a restart lsp for workspace tras instalar, pero no encontré el disparo para no responde unresponsive : clangd index healthy solo devuelve un bool y no llama al reinicio, y clangd process alive no se leyó correctamente peek truncado erróneo .
Abierto:
- pending lsp restart
- clangd index healthy callers
- run loop check pending lsp restart

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje no responde para disparar el reinicio
keep: M5
leído: clangd process alive, clangd index healthy
Cerrado:
leído: clangd process alive, clangd index healthy
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se lee el estado de clangd process alive para decidir si reiniciar
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: clangd process alive, clangd index healthy
    extra: read file text, english strings

T4
    visto: clangd process alive
    extra: read file text, english strings

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: clangd process alive
  T1=>T4  mismo objeto: clangd process alive
  T2=>T3  mismo objeto: clangd process alive
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd process alive
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → read file text
  T3 → english strings
