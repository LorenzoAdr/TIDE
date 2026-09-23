### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M7
leído: close, set pos, reset to single cursor
Cerrado:
leído: close, set pos, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al iniciar el proyecto
keep: M7
leído: reopen workspace documents, run
Cerrado:
encontré el mecanismo de restauración: reopen workspace documents itera workspace tabs y llama a symbols document opened para cada archivo con path no vacío. El disparo se confirma en run rama ensure backend started que invoca a reopen workspace documents tras iniciar el backend.
Abierto:
- restart lsp for workspace
- ensure backend started

### Trabajo 3
consulta: dónde se controla la visibilidad de los paneles laterales al cargar la sesión
keep: M1 M6
leído: reopen workspace documents, run
Cerrado:
leído: reopen workspace documents, run
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close, set pos, reset to single cursor
    extra: request start, App Session

T2
    visto: reopen workspace documents, run
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: reopen workspace documents, run
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  Make Main Layout → Make Editor Panel → handle editor mouse → reset to single cursor → set pos
  T1=>T3  Make Main Layout → Make Editor Panel → handle editor mouse → reset to single cursor → set pos
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T1 → request start
  T1 → App Session
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → restart lsp for workspace
  T3 → on lsp missing install
