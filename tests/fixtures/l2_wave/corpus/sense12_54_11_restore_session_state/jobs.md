### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: reopen workspace documents, flush active tab, open workspace wizard
Cerrado:
leído: reopen workspace documents, flush active tab, open workspace wizard
Abierto:
- restart lsp for workspace

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: reopen workspace documents, run, restore workspace session
Cerrado:
encontré el mecanismo: restore workspace session en application carga load y itera session open tabs llamando a workspace open file para cada archivo válido. El disparo ocurre en el constructor Application y en set workspace .
Abierto:
- restart lsp for workspace
- open file
- set workspace

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M7
leído: restore workspace session, open workspace wizard
Cerrado:
leído: restore workspace session, open workspace wizard
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: reopen workspace documents, flush active tab, open workspace wizard
    extra: restart lsp for workspace, on lsp missing install

T2
    visto: reopen workspace documents, run, restore workspace session
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: restore workspace session, open workspace wizard
    extra: Application, set workspace

entre abiertas:
  T1=>T2  mismo objeto: reopen workspace documents
  T1=>T2  restore workspace session → open file → flush active tab
  T1=>T3  mismo objeto: open workspace wizard
  T1=>T3  restore workspace session → open file → flush active tab
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → restart lsp for workspace
  T1 → on lsp missing install
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → Application
  T3 → set workspace
