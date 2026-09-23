### Trabajo 1
consulta: dónde se pinta la línea roja en el margen del editor para marcar un error
keep: M1 M5
leído: line diagnostic marker from map, collect diagnostic decorations, line gutter marker, Render Editor Line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: la línea roja es en realidad un carácter pintado en el margen gutter . El flujo es: 1 line diagnostic marker from map editor panel cpp consulta los diagnostics de la línea y devuelve si hay un error severity k Error . 2 line gutter marker editor panel cpp llama a la anterior y retorna ese como marcador del gutter. 3 Este carácter se integra en la UI del margen. No hay una línea roja gráfica continua, sino un marcador de texto rojo implícito por el contexto de error en el gutter.
Abierto:
- render gutter line

### Trabajo 2
consulta: dónde se extraen los errores de compilación de la consola para llenar el mapa de diagnostics del editor
keep: M5 M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación de la consola para llenar el mapa de diagnostics del editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se analizan los errores de compilación del output de la consola para generar diagnostics
keep: M1 M5
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line diagnostic marker from map, collect diagnostic decorations, line gutter marker, Render Editor Line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

T3
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T3 → tick binary symbols panel
  T3 → has value
