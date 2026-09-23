### Trabajo 1
consulta: dónde se extraen la línea y columna de los errores de compilación del texto de la consola
keep: M11
leído: diagnostics for editor line
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen la línea y columna de los errores de compilación del texto de la consola
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M1
leído: diagnostics for editor line, Make Editor Panel
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
no encontré el mecanismo que dibuja la línea roja en el margen; lo leído son funciones de consulta de diagnósticos y construcción del panel, pero no la lógica de pintura del margen
Abierto:
- paint margin
- draw error indicator
- viewport line render cache

### Trabajo 3
consulta: dónde se pinta el indicador de error en el margen del editor al recibir un diagnóstico
keep: M1
leído: line gutter marker, gutter buffer line at row
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se pinta el indicador de error en el margen del editor al recibir un diagnóstico
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: line gutter marker, gutter buffer line at row
Abierto:
- handle editor mouse

### Trabajo 4
consulta: dónde se crea el marcador de diagnóstico en el margen del editor al recibir un error
keep: M1
leído: line diagnostic marker, line diagnostic marker from map, line gutter marker
Cerrado:
encontré el mecanismo: el marcador se crea en line gutter marker que es invocado desde el bucle de pintura de Make Editor Panel línea 7310 para cada línea del viewport; este función delega en line diagnostic marker from map para obtener el carácter o W basado en los diagnósticos.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, Make Editor Panel
    extra: Make Main Layout, ensure buffer

T3
    visto: line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

T4
    visto: line diagnostic marker, line diagnostic marker from map, line gutter marker
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
  T1=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T1=>T4  line diagnostic marker from map → diagnostics for editor line
  T2=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T2=>T4  line diagnostic marker from map → diagnostics for editor line
  T3=>T4  mismo objeto: line gutter marker
hacia el resto:
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → ensure buffer
  T3 → Make Main Layout
  T3 → git line changed
  T4 → Make Main Layout
