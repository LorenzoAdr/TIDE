### Trabajo 1
consulta: dónde se leen los errores de compilación del texto de la consola inferior
keep: M11
leído: problems tab active, Event, problems tab active console, Render
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de lectura de errores de compilación; lo leído son funciones de renderizado pintado y gestión de eventos de teclado en la pestaña de problemas, pero no el código que parsea o extrae los errores del texto de la consola inferior.
Abierto:
- parse errors
- update from console

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M4 M7
leído: line gutter marker, fold gutter marker, line diagnostic marker from map, line diagnostic marker
Cerrado:
leído: line gutter marker, fold gutter marker, line diagnostic marker from map, line diagnostic marker
Abierto:
- handle fold gutter click

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M1
leído: diagnostics for translation unit, parse gcc style, parse gfortran stderr, find compiler location, terminal row link
Cerrado:
encontré el mecanismo: la extracción de errores de compilación del texto de la consola ocurre en find compiler location que es invocado por terminal row link al procesar cada línea terminal styled rows de la consola. find compiler location delega en parsers específicos como parse gcc style para GCC Clang y parse msvc style para MSVC, los cuales extraen ruta, línea y columna del texto plano. Existe también un flujo paralelo para Fortran en parse gfortran stderr que parsea stderr text completo, pero el caso general de consola se resuelve línea a línea en compiler location .
Abierto:
- run gfortran diagnostics


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: problems tab active, Event, problems tab active console, Render
    extra: Make Busy Strip Placeholder, colors revision

T2
    visto: line gutter marker, fold gutter marker, line diagnostic marker from map, line diagnostic marker
    extra: Make Editor Panel, Make Main Layout

T3
    visto: diagnostics for translation unit, parse gcc style, parse gfortran stderr, find compiler location
    extra: Make Main Layout, build include tree

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Busy Strip Placeholder
  T1 → colors revision
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → Make Main Layout
  T3 → build include tree
