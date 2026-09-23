### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M12
leído: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
Cerrado:
leído: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1 M2
leído: line gutter marker
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la pintura de la línea roja; lo leído es un selector de carácter ASCII G, W para el gutter, no la lógica de renderizado de color
Abierto:
- paint gutter
- draw line marker
- gutter color

### Trabajo 3
consulta: dónde se crea el marcador de línea en el margen del editor para un error de diagnóstico
keep: M1 M2
leído: line diagnostic marker, line gutter marker, Make Editor Panel
Cerrado:
leído: line diagnostic marker, line gutter marker, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
    extra: build console panel view, render app terminal

T2
    visto: line gutter marker
    extra: Make Editor Panel, Make Main Layout

T3
    visto: line diagnostic marker, line gutter marker, Make Editor Panel
    extra: Make Main Layout, git line changed

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  Make Editor Panel → sync diagnostic cache → diagnostics for translation unit
  T2=>T3  mismo objeto: line gutter marker
hacia el resto:
  T1 → build console panel view
  T1 → render app terminal
  T2 → Make Main Layout
  T2 → git line changed
  T3 → Make Main Layout
  T3 → git line changed
