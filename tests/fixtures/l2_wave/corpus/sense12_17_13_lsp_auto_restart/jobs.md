### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el proceso del servidor LSP y cómo se monitoriza su estado para detectar fallos
keep: M5
leído: restart lsp for workspace, run, on lsp missing install, on workspace opened, schedule debounced lsp restart
Cerrado:
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o no responde y se lanza el reinicio automático
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart

T2
    visto: restart lsp for workspace, run, on lsp missing install, on workspace opened
    extra: set workspace clangd options, ensure compile commands for clangd

T3
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T2 → set workspace clangd options
  T2 → ensure compile commands for clangd
  T3 → set workspace clangd options
  T3 → ensure compile commands for clangd
