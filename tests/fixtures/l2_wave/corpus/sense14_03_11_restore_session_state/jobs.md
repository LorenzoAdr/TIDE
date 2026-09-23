### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M10
leído: open file at impl, load buffer from disk, reset to single cursor
Cerrado:
encontré el mecanismo: la posición del cursor se restaura en open file at impl y load buffer from disk llamando a reset to single cursor que limpia los cursores y establece la posición inicial.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al cargar un proyecto
keep: M5 M8
leído: reopen workspace documents, open file at impl, load buffer from disk
Cerrado:
encontré el objeto de la consulta: la restauración de archivos al cargar un proyecto se orquesta en reopen workspace documents que itera sobre workspace tabs y notifica al proveedor de símbolos document opened para cada ruta no vacía, tras limpiar la pestaña activa con flush active tab . Los buffers se cargan en disco mediante load buffer from disk llamado indirectamente vía open file impl en el flujo de apertura, aunque reopen workspace documents asume que los buffers ya existen en la estructura de tabs y solo sincroniza el estado con el LSP símbolos .
Abierto:
- restart lsp for workspace
- open file impl
- flush active tab

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al cargar un proyecto
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al cargar un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: open file at impl, load buffer from disk, reset to single cursor
    extra: open file at confirmed, flush active tab

T2
    visto: reopen workspace documents, open file at impl, load buffer from disk
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: open file at impl
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → open file at confirmed
  T1 → flush active tab
  T2 → restart lsp for workspace
  T2 → on lsp missing install
