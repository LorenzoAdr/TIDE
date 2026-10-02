### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M9
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detectan los errores de compilación en la salida de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M8 M10
leído: visual highlight service, diagnostics on line, handle gutter marker click, handle editor mouse, line gutter marker, line diagnostic marker from map
Cerrado:
leído: visual highlight service, diagnostics on line, handle gutter marker click, handle editor mouse, line gutter marker, line diagnostic marker from map
Abierto:
- end mouse selection

### Trabajo 3
consulta: dónde se extrae la línea de error del texto de la consola para marcarla en el editor
keep: M1 M5
leído: make terminal panel
Cerrado:
leído: make terminal panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: visual highlight service, diagnostics on line, handle gutter marker click, handle editor mouse
    extra: Make Editor Panel, tick visual highlight scheduler

T3
    visto: make terminal panel
    extra: Code Bg, Panel Title

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → Make Editor Panel
  T2 → tick visual highlight scheduler
  T3 → Code Bg
  T3 → Panel Title
