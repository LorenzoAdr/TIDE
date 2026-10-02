### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor posición del cursor y archivos abiertos al cerrar y abrir un proyecto
keep: M1 M12
leído: save workspace session, flush active tab, save, restore workspace session
Cerrado:
leído: save workspace session, flush active tab, save, restore workspace session
Abierto:
- run custom event drain

### Trabajo 2
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M8 M10
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, flush active tab, save, restore workspace session
    extra: run custom event drain, apply pending connection

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
