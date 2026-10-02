### Trabajo 1
consulta: dónde se extraen la línea y el número de columna de los errores de compilación del texto de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré la extracción de línea y columna de errores de compilación; lo leído es un escáner de errores de enlazador linker que extrae solo símbolos indefinidos, no coordenadas de fuente
Abierto:
- parse linker undefined reference
- compilation error parser

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M1
leído: diagnostics for editor line, diagnostics on line
Cerrado:
leído: diagnostics for editor line, diagnostics on line
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se crea el objeto de diagnóstico que contiene la línea y columna del error para que el editor lo pinte
keep: M1
leído: diagnostics on line, diagnostics for editor line, diagnostics hpp, editor panel hpp
Cerrado:
leído: diagnostics on line, diagnostics for editor line, diagnostics hpp, editor panel hpp
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 4
consulta: dónde se parsean los errores de compilación del texto de la consola para extraer línea y columna
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para extraer línea y columna
Un hop no leído no es un hueco. Cierra el claim solo si lo acumulado basta; si no, otra pregunta.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: diagnostics for editor line, diagnostics on line
    extra: Make Editor Panel, Make Main Layout

T3
    visto: diagnostics on line, diagnostics for editor line, diagnostics hpp, editor panel hpp
    extra: handle gutter marker click, handle editor mouse

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: diagnostics for editor line
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → handle gutter marker click
  T3 → handle editor mouse
