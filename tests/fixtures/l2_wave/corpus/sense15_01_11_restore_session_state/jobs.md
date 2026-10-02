### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M7 M8
leído: editor state cpp, helix state cpp
Cerrado:
leído: editor state cpp, helix state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: M1 M6
leído: reopen workspace documents, run, on workspace complete, restart lsp for workspace
Cerrado:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos ocurre en reopen workspace documents que es invocado por restart lsp for workspace tras cargar el workspace y por run inicio de la app . El mecanismo itera sobre workspace tabs y llama a symbols document opened para cada archivo.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
keep: M8 M10
leído: reopen workspace documents, on workspace complete
Cerrado:
leído: reopen workspace documents, on workspace complete
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, helix state cpp

T2
    visto: reopen workspace documents, run, on workspace complete, restart lsp for workspace
    extra: on lsp missing install, on document opened

T3
    visto: reopen workspace documents, on workspace complete
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T2 → on lsp missing install
  T2 → on document opened
  T3 → on lsp missing install
  T3 → on document opened
