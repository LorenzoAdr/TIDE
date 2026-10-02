### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: on lsp missing install, on lsp missing bundle
Cerrado:
leído: on lsp missing install, on lsp missing bundle
Abierto:
- begin shutdown

### Trabajo 2
consulta: dónde se inicia el reinicio del servidor LSP tras detectar que falta o ha caído
keep: M5
leído: on lsp missing install, on lsp missing bundle, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
encontré el mecanismo de reinicio tras detección de caída on lsp missing install llama a restart lsp for workspace tras instalación exitosa; on lsp missing bundle inicia shutdown tras configurar bundle; schedule debounced lsp restart programa un reinicio diferido 2s llamado desde setup build environment watching
Abierto:
- process build environment updates


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp missing install, on lsp missing bundle, restart lsp for workspace, schedule debounced lsp restart
    extra: clear busy, install language pack

T2
    visto: on lsp missing install, on lsp missing bundle, restart lsp for workspace, schedule debounced lsp restart
    extra: clear busy, install language pack

entre abiertas:
  T1=>T2  mismo objeto: on lsp missing install
hacia el resto:
  T1 → clear busy
  T1 → install language pack
  T2 → clear busy
  T2 → install language pack
