### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor text hpp, load working lines from disk, reset to single cursor
Cerrado:
leído: editor text hpp, load working lines from disk, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: M1 M6
leído: reopen workspace documents, load
Cerrado:
encontré el mecanismo: la restauración de la lista de archivos abiertos al volver a abrir un proyecto ocurre en reopen workspace documents . Esta función itera sobre workspace tabs que contiene los archivos del proyecto y llama a symbols document opened para cada uno, restaurando así el estado de los documentos. La función load lee la lista de proyectos recientes, no los archivos abiertos de un proyecto específico.
Abierto:
- restart lsp for workspace

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M7
leído: load working lines from disk, load, reopen workspace documents
Cerrado:
leído: load working lines from disk, load, reopen workspace documents
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor text hpp, load working lines from disk, reset to single cursor
    extra: open git diff view, open git diff tab

T2
    visto: reopen workspace documents, load
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: load working lines from disk, load, reopen workspace documents
    extra: open git diff view, open git diff tab

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: load working lines from disk
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → open git diff view
  T3 → open git diff tab
