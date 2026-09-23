### Trabajo 1
consulta: dónde se detectan los errores de compilación en la consola inferior
keep: M9 M10
leído: diagnostics panel cpp, console panel tab active, console panel cpp, diagnostics panel hpp
Cerrado:
leído: diagnostics panel cpp, console panel tab active, console panel cpp, diagnostics panel hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M6 M7
leído: line gutter marker, gutter buffer line at row
Cerrado:
leído: line gutter marker, gutter buffer line at row
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se extrae la línea del error del texto de la consola
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extrae la línea del error del texto de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics panel cpp, console panel tab active, console panel cpp, diagnostics panel hpp
    extra: Make Busy Strip Placeholder, colors revision

T2
    visto: line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Busy Strip Placeholder
  T1 → colors revision
  T2 → Make Editor Panel
  T2 → Make Main Layout
