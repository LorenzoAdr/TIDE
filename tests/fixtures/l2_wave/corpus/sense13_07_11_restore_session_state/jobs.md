### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cargar un proyecto
keep: M11
leído: editor state cpp, editor state hpp
Cerrado:
leído: editor state cpp, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la posición del cursor al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la lista de archivos abiertos al cargar un proyecto
keep: M6
leído: editor state hpp, editor state cpp, load, remember, config path, save
Cerrado:
encontré el mecanismo de guardado y restauración: la lista se persiste en un archivo JSON en recent projects json ruta dada por config path . La restauración ocurre en load lee el JSON y llena recent.paths y el guardado en save escribe recent.paths al JSON, invocado por remember
Abierto:
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, editor state hpp

T2
    visto: (nada)

T3
    visto: editor state hpp, editor state cpp, load, remember
    extra: ensure wake fd, open host pty

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: editor state cpp
  T2=>T3  sin camino
hacia el resto:
  T3 → ensure wake fd
  T3 → open host pty
