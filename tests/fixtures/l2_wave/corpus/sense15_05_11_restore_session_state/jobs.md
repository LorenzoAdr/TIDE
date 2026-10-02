### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M7 M8
leído: editor state cpp, helix state cpp
Cerrado:
leído: editor state cpp, helix state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al iniciar la sesión
keep: M1 M3
leído: reopen workspace documents, run, restore workspace session
Cerrado:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos ocurre en restore workspace session que carga load y itera sobre session open tabs llamando a workspace open file para cada uno.
Abierto:
- restart lsp for workspace

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al iniciar la sesión
keep: M8 M11
leído: restore workspace session, editor state cpp, helix state cpp, Application
Cerrado:
leído: restore workspace session, editor state cpp, helix state cpp, Application
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, helix state cpp

T2
    visto: reopen workspace documents, run, restore workspace session
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: restore workspace session, editor state cpp, helix state cpp, Application
    extra: set workspace, open file

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: editor state cpp
  T2=>T3  mismo objeto: restore workspace session
  T2=>T3  Application → set workspace → maybe generate host compile commands → restart lsp for workspace → reopen workspace documents
hacia el resto:
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → set workspace
  T3 → open file
