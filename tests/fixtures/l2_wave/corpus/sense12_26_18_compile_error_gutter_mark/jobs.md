### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, handle gutter marker click
Cerrado:
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, handle gutter marker click
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: note console click, handle console panel mouse, apply console multi click
Cerrado:
leído: note console click, handle console panel mouse, apply console multi click
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para extraer la línea y el mensaje
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para extraer la línea y el mensaje
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, gutter buffer line at row, line diagnostic marker from map, handle gutter marker click
    extra: Make Editor Panel, Make Main Layout

T2
    visto: note console click, handle console panel mouse, apply console multi click
    extra: console same click spot, Is Empty

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → console same click spot
  T2 → Is Empty
