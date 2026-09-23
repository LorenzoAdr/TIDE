### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
keep: M3 M11
leído: close, editor panel cpp
Cerrado:
leído: close, editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor y los archivos abiertos al salir del proyecto
keep: M1 M9
leído: save, config path, save workspace session, flush active tab
Cerrado:
leído: save, config path, save workspace session, flush active tab
Abierto:
- run background generation
- run custom event drain

### Trabajo 3
consulta: dónde se restaura la posición del cursor y los archivos abiertos al entrar al proyecto
keep: M6 M8
leído: reload stale tabs from disk
Cerrado:
leído: reload stale tabs from disk
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close, editor panel cpp
    extra: App Session, request start

T2
    visto: save, config path, save workspace session, flush active tab
    extra: run background generation, update active environment

T3
    visto: reload stale tabs from disk
    extra: is open, is tabular path

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → App Session
  T1 → request start
  T2 → run background generation
  T2 → update active environment
  T3 → is open
  T3 → is tabular path
