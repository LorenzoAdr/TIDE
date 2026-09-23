### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1
leído: line diagnostic marker, line diagnostic marker from map, line gutter marker, Make Editor Panel
Cerrado:
encontré el mecanismo: la línea roja carácter se determina en line diagnostic marker from map que devuelve para errores y se consume en line gutter marker el cual es llamado dentro del bucle de renderizado de Make Editor Panel para asignar el marcador al gutter de cada línea visible.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se extraen los errores de la consola para crear los diagnósticos del editor
keep: M1 M7
leído: line diagnostic marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo que extrae errores de la consola para crear diagnósticos del editor; lo leído muestra que los diagnósticos del editor provienen exclusivamente de LSP apply lsp diagnostics to panel o de una caché de archivos push active file diagnostics from cache, sin ningún puente desde la consola
Abierto:
- console to editor diagnostics
- parse console errors to diagnostics

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para generar diagnósticos
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para generar diagnósticos
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo que parsea errores de compilación de la consola para generar diagnósticos del editor; lo leído muestra que scan shell output for linker errors parsea referencias indefinidas de enlazador para análisis de símbolos binarios, no para diagnósticos de línea en el editor
Abierto:
- parse compilation errors
- console to diagnostics

### Trabajo 4
consulta: dónde se suscribe el editor a los cambios de texto de la consola para detectar errores
keep: M1 M7
leído: Make Editor Panel, console panel
Cerrado:
leído: Make Editor Panel, console panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: line diagnostic marker, line diagnostic marker from map, line gutter marker, Make Editor Panel
    extra: Make Main Layout, diagnostics for editor line

T2
    visto: line diagnostic marker, line diagnostic marker from map, diagnostics for editor line, rebuild diagnostics by line
    extra: Make Editor Panel, line gutter marker

T3
    visto: (nada)

T4
    visto: Make Editor Panel, console panel
    extra: Make Main Layout, ensure buffer

entre abiertas:
  T1=>T2  mismo objeto: line diagnostic marker
  T1=>T2  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T1=>T3  sin camino
  T1=>T4  mismo objeto: Make Editor Panel
  T2=>T3  sin camino
  T2=>T4  Make Editor Panel → line gutter marker → line diagnostic marker from map
  T3=>T4  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T2 → Make Main Layout
  T2 → apply lsp diagnostics to panel
  T4 → Make Main Layout
  T4 → ensure buffer
