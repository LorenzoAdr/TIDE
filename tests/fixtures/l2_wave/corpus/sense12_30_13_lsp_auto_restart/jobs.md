### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder y cómo se gestiona su reinicio
keep: M5
leído: transport running, send notification, restart lsp for workspace, schedule debounced lsp restart, is running
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de detección y gestión: la caída se detecta en async worker main cuando job lsp transport running devuelve falso lo que implica que is running es falso, típicamente por fallo en poll o cierre de fd en el reader thread, estableciendo pending transport restart true la gestión del reinicio se orquesta en restart lsp for workspace que se dispara tras la detección vía ensure async worker running - start async worker - async worker main - detección - flag - loop de aplicación que llama a restart lsp for workspace si pending lsp restart es true, aunque el caller exacto del flag a la acción de reinicio en Application no está completamente leído, el circuito ON OFF y el bosquej
Abierto:
- send cancel
- start async worker
- pending transport restart
- pending lsp restart


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: transport running, send notification, restart lsp for workspace, schedule debounced lsp restart
    extra: ensure python lsp async, start async worker
    entre interno: restart lsp for workspace → send notification
hacia el resto:
  T1 → ensure python lsp async
  T1 → start async worker
