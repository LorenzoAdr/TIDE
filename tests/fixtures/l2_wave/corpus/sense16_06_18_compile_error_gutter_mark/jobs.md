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
consulta: dónde se dibuja la línea roja en el margen del editor
keep: M1 M9
leído: Editor Line Hi
Cerrado:
leído: Editor Line Hi
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la salida del terminal para extraer la línea y el mensaje
keep: M1 M4
leído: parse gcc style, parse msvc style, find compiler location, terminal row link
Cerrado:
encontré el mecanismo de parsing: find compiler location en compiler location es el dispatcher que invoca a parse gcc style y parse msvc style para extraer ruta, línea y columna de la salida del terminal; el punto de entrada desde la UI es terminal row link en console panel
Abierto:
- parse trailing location
- find gcc severity


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: Editor Line Hi
    extra: Make Editor Panel, make completion overlay

T3
    visto: parse gcc style, parse msvc style, find compiler location, terminal row link
    extra: parse positive int, pop back

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → Make Editor Panel
  T2 → make completion overlay
  T3 → parse positive int
  T3 → pop back
