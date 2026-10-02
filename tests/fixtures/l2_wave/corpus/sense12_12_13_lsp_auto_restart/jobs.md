### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: quién llama a schedule debounced lsp restart cuando falta la instalación del LSP
keep: M5
leído: lsp missing prompt for status key, maybe show lsp missing toast
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré que nadie llame a schedule debounced lsp restart cuando falta la instalación del LSP; lo leído muestra que maybe show lsp missing toast disparado por falta de LSP solo muestra un toast y no llama a schedule debounced lsp restart, y los callers de schedule debounced lsp restart son setup build environment watching cambio de entorno y el constructor de Application inicio, ninguno relacionado con lsp missing
Abierto:
- lsp missing prompt for status key callers en application cpp


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart

T3
    visto: lsp missing prompt for status key, maybe show lsp missing toast, schedule debounced lsp restart
    extra: is lsp missing status key, Application

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T3 → is lsp missing status key
  T3 → Application
