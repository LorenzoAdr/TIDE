### Trabajo 1
consulta: dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de extracción de línea y mensaje de errores de compilación; lo leído es scan shell output for linker errors que extrae símbolos de referencias indefinidas del linker, no errores de compilación línea mensaje
Abierto:
- parse compilation error
- extract error line and message

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1
leído: editor panel cpp, editor panel hpp
Cerrado:
leído: editor panel cpp, editor panel hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1
leído: editor panel hpp, line diagnostic marker, line diagnostic marker from map, line gutter marker
Cerrado:
leído: editor panel hpp, line diagnostic marker, line diagnostic marker from map, line gutter marker
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se parsean los errores de compilación para extraer la línea y el mensaje del texto de la consola
keep: M1
leído: line diagnostic marker, diagnostics for editor line, viewport line hash diagnostics
Cerrado:
leído: line diagnostic marker, diagnostics for editor line, viewport line hash diagnostics
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: editor panel cpp, editor panel hpp

T3
    visto: editor panel hpp, line diagnostic marker, line diagnostic marker from map, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T4
    visto: line diagnostic marker, diagnostics for editor line, viewport line hash diagnostics
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: editor panel hpp
  T2=>T4  sin camino
  T3=>T4  mismo objeto: line diagnostic marker
  T3=>T4  line diagnostic marker from map → diagnostics for editor line
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T3 → Make Editor Panel
  T3 → Make Main Layout
  T4 → Make Editor Panel
  T4 → Make Main Layout
