### Trabajo 1
consulta: dónde se extrae la línea de error del texto de la consola inferior
keep: M7
leído: terminal selected text, console selection line, terminal row text
Cerrado:
encontré el mecanismo de extracción del texto de la consola: terminal selected text itera sobre las filas seleccionadas, delega en console selection line para obtener el string de cada fila que a su vez delega en terminal row text para concatenar los spans de texto plano, y aplica los cortes de columna substr para devolver el texto final. No hay una lógica específica de línea de error en esta cadena; es una extracción genérica de texto seleccionado.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen del editor
keep: M1
leído: terminal selected text, console selection line, terminal row text
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré el dibujo de la línea roja en el margen; lo leído son funciones de extracción de texto de la consola terminal selected text, console selection line, terminal row text que no contienen lógica de pintura ni referencias a márgenes errores
Abierto:
- paint margin red line
- draw error indicator
- editor gutter paint

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen del editor para marcar un error
keep: M1 M3
leído: diagnostics for editor line, diagnostics display allowed, diagnostic severity label
Cerrado:
leído: diagnostics for editor line, diagnostics display allowed, diagnostic severity label
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se parsea la salida de la consola para extraer la línea y columna del error de compilación
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré el parsing de errores de compilación línea columna; lo leído es scan shell output for linker errors que parsea referencias indefinidas de enlazado, no errores de compilación genéricos
Abierto:
- parse linker undefined reference


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: terminal selected text, console selection line, terminal row text
    extra: copy terminal selection, forward pty key

T2
    visto: terminal selected text, console selection line, terminal row text
    extra: copy terminal selection, forward pty key

T3
    visto: diagnostics for editor line, diagnostics display allowed, diagnostic severity label
    extra: Make Editor Panel, Make Main Layout

T4
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

entre abiertas:
  T1=>T2  mismo objeto: terminal selected text
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → copy terminal selection
  T1 → forward pty key
  T2 → copy terminal selection
  T2 → forward pty key
  T3 → Make Editor Panel
  T3 → Make Main Layout
