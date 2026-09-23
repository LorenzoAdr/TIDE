### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
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
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se invoca lsp missing install cuando falta el servidor LSP
keep: M1
leído: lsp missing prompt for status key, maybe show lsp missing toast, Application
Cerrado:
encontré el objeto de la consulta: la invocación ocurre en el constructor de Application Application, línea 327, donde se detecta que falta el LSP y se encola una tarea UI para llamar a maybe show lsp missing toast
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart

T3
    visto: lsp missing prompt for status key, maybe show lsp missing toast, Application
    extra: is lsp missing status key, find tuide source root

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  Application → set workspace → maybe generate host compile commands → restart lsp for workspace
  T2=>T3  Application → set workspace → setup build environment watching → schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T3 → is lsp missing status key
  T3 → find tuide source root
