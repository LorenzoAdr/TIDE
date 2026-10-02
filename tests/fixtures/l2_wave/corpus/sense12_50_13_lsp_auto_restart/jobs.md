### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje falta o se ha caído para mostrar el aviso
keep: M1
leído: lsp missing prompt for status key, maybe show lsp missing toast, Application
Cerrado:
leído: lsp missing prompt for status key, maybe show lsp missing toast, Application
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje falta para disparar el reinicio
keep: M1 M5
leído: lsp missing prompt for status key, maybe show lsp missing toast, is lsp missing status key, catalog, restart lsp for workspace, on lsp missing install
Cerrado:
encontré el objeto de la consulta: la detección ocurre en maybe show lsp missing toast que consulta lsp missing prompt for status key contra el catalog y el disparo del reinicio se ejecuta en lsp missing install tras una instalación exitosa.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: lsp missing prompt for status key, maybe show lsp missing toast, Application
    extra: is lsp missing status key, find tuide source root

T3
    visto: lsp missing prompt for status key, maybe show lsp missing toast, is lsp missing status key, catalog
    extra: Application, find tuide source root

entre abiertas:
  T1=>T2  Application → set workspace → maybe generate host compile commands → restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: lsp missing prompt for status key
  T2=>T3  Application → maybe show lsp missing toast → lsp missing prompt for status key
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → find tuide source root
  T2 → absolute
  T3 → find tuide source root
  T3 → has value
