### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir el proyecto
keep: M2 M12
leído: save
Cerrado:
leído: save
Abierto:
- run background generation

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al volver a abrir el proyecto
keep: M8 M10
leído: reopen workspace documents, run, flush active tab, restart lsp for workspace, on lsp missing install, on workspace complete
Cerrado:
leído: reopen workspace documents, run, flush active tab, restart lsp for workspace, on lsp missing install, on workspace complete
Abierto:
- ensure backend started

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir el proyecto
keep: M8 M12
leído: open workspace wizard, reopen workspace documents
Cerrado:
leído: open workspace wizard, reopen workspace documents
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save
    extra: run background generation, update active environment

T2
    visto: reopen workspace documents, run, flush active tab, restart lsp for workspace
    extra: on document opened, editor buffer text

T3
    visto: open workspace wizard, reopen workspace documents
    extra: exit debug mode, Raw Pty Screen

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T1 → run background generation
  T1 → update active environment
  T2 → on document opened
  T2 → editor buffer text
  T3 → exit debug mode
  T3 → Raw Pty Screen
