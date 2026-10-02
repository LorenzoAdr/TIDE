### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace
Cerrado:
leído: restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a restart lsp for workspace cuando el servidor se cae o no responde
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: quién llama a restart lsp for workspace cuando el servidor se cae o no responde
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor LSP no responde o se cae para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
encontré el mecanismo de detección y disparo: process build environment updates actúa como el detector lee pending lsp restart y lsp restart deadline para verificar timeout y dispara el reinicio vía restart lsp for workspace . La programación del debounce ocurre en schedule debounced lsp restart .
Abierto:
- run custom event drain
- pending lsp restart
- lsp restart deadline


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T3 → on lsp missing install
  T3 → on workspace opened
