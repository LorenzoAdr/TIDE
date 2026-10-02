### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M3 M12
leído: reopen workspace documents, load working lines from disk
Cerrado:
leído: reopen workspace documents, load working lines from disk
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al volver a un proyecto
keep: M3 M7
leído: load, run, restore workspace session
Cerrado:
encontré el mecanismo: restore workspace session lee load y itera session open tabs llamando a workspace open file path para cada archivo existente; se dispara desde set workspace y el constructor Application.
Abierto:
- open file

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M8 M10
leído: reopen workspace documents, load working lines from disk, restore workspace session, load
Cerrado:
leído: reopen workspace documents, load working lines from disk, restore workspace session, load
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: reopen workspace documents, load working lines from disk
    extra: restart lsp for workspace, on lsp missing install

T2
    visto: load, run, restore workspace session
    extra: ensure wake fd, open host pty

T3
    visto: reopen workspace documents, load working lines from disk, restore workspace session, load
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  reindex project → reopen workspace documents
  T1=>T3  mismo objeto: reopen workspace documents
  T2=>T3  mismo objeto: load
  T2=>T3  reindex project → reopen workspace documents
hacia el resto:
  T1 → restart lsp for workspace
  T1 → on lsp missing install
  T2 → ensure wake fd
  T2 → open host pty
  T3 → restart lsp for workspace
  T3 → on lsp missing install
