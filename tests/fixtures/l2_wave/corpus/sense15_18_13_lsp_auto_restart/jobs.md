### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: start completion server, status text, ensure completion server, health ok
Cerrado:
leído: start completion server, status text, ensure completion server, health ok
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje cuando cambia el entorno
keep: M1 M2
leído: schedule debounced lsp restart, setup build environment watching
Cerrado:
encontré dónde se programa el reinicio diferido: en setup build environment watching se registra el callback set environment changed callback que invoca schedule debounced lsp restart, y en set change callback del watcher de artefactos también se invoca schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart cuando el servidor de lenguaje se cae o no responde
keep: M1 M4
leído: schedule debounced lsp restart, on lsp notification, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, on lsp notification, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: start completion server, status text, ensure completion server, health ok
    extra: stop listener on port, stop owned unlocked

T2
    visto: schedule debounced lsp restart, setup build environment watching
    extra: apply workspace settings, Workspace Config

T3
    visto: schedule debounced lsp restart, on lsp notification, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → stop listener on port
  T1 → stop owned unlocked
  T2 → apply workspace settings
  T2 → Workspace Config
  T3 → apply workspace settings
  T3 → Workspace Config
