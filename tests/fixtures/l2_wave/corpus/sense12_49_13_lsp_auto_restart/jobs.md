### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o no responde y se dispara el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o no responde y se dispara el reinicio
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo o reinicio de la aplicación
keep: M5
leído: restart lsp for workspace, on lsp missing install, run, schedule debounced lsp restart
Cerrado:
encontré el mecanismo de reinicio del LSP tras fallo o cambio de entorno: el efecto es restart lsp for workspace lee config, llama a workspace opened en el provider y reabre docs; los disparos son lsp missing install tras instalación de toolpack y schedule debounced lsp restart tras cambios en settings o entorno build, con debounce de 2s . El flujo está completo.
Abierto:
- process build environment updates


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: restart lsp for workspace, on lsp missing install, run, schedule debounced lsp restart
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → on workspace opened
  T2 → set workspace clangd options
