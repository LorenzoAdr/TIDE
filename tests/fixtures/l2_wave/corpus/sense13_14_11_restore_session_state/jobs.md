### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto para restaurarla al abrirlo
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda la posición del cursor al cerrar el proyecto para restaurarla al abrirlo
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guardan los archivos abiertos al cerrar el proyecto para restaurarlos al abrirlo
keep: M4
leído: remember, load, config path, save
Cerrado:
encontré el mecanismo: save persiste open tabs y active tab path en un JSON dentro de .tuide del workspace, y load los restaura
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda el estado de los paneles laterales al cerrar el proyecto para restaurarlos al abrirlo
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda el estado de los paneles laterales al cerrar el proyecto para restaurarlos al abrirlo
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: remember, load, config path, save
    extra: is directory, normalize workspace path

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → is directory
  T2 → normalize workspace path
