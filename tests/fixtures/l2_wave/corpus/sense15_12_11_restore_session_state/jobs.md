### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M4 M11
leído: stabilize scroll after fold change, editor text hpp
Cerrado:
leído: stabilize scroll after fold change, editor text hpp
Abierto:
- handle fold gutter click

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1 M3
leído: reopen workspace documents
Cerrado:
leído: reopen workspace documents
Abierto:
- restart lsp for workspace

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1 M4
leído: reopen workspace documents, run, load, apply connection and start
Cerrado:
leído: reopen workspace documents, run, load, apply connection and start
Abierto:
- restart lsp for workspace
- run custom event drain
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: stabilize scroll after fold change, editor text hpp
    extra: handle fold gutter click, handle editor mouse

T2
    visto: reopen workspace documents
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: reopen workspace documents, run, load, apply connection and start
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  Make Main Layout → Make Editor Panel → handle editor mouse → handle fold gutter click → stabilize scroll after fold change
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T1 → handle fold gutter click
  T1 → handle editor mouse
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → restart lsp for workspace
  T3 → on lsp missing install
