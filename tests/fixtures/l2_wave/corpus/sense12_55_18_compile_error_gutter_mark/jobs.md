### Trabajo 1
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M2
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel
Cerrado:
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola inferior
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola inferior
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se lee el texto de salida de la consola inferior para procesarlo
keep: M12
leído: set inferior pid, source panel contains mouse, on pty output
Cerrado:
leído: set inferior pid, source panel contains mouse, on pty output
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel
    extra: Make Main Layout, git line changed

T2
    visto: (nada)

T3
    visto: set inferior pid, source panel contains mouse, on pty output
    extra: open for pid, refresh connections

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T3 → open for pid
  T3 → refresh connections
