### Trabajo 1
consulta: dónde se procesa la salida de texto de la consola inferior para extraer mensajes
keep: M7 M9
leído: rebuild display, styled rows, rebuild cache, rebuild display locked, scan shell output for linker errors, drain output bytes
Cerrado:
leído: rebuild display, styled rows, rebuild cache, rebuild display locked, scan shell output for linker errors, drain output bytes
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibujan las marcas de colores en el margen izquierdo del editor
keep: M1 M3
leído: build git marks snapshot
Cerrado:
leído: build git marks snapshot
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se actualiza el estado de las marcas visuales en el margen del editor para reflejar errores
keep: M3
leído: diagnostics on line, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed
Cerrado:
leído: diagnostics on line, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed
Abierto:
- handle editor mouse

### Trabajo 4
consulta: dónde se parsean los errores de compilación de la salida de la consola para extraer la línea y el mensaje
keep: M11
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild display, styled rows, rebuild cache, rebuild display locked
    extra: display text, build all rows

T2
    visto: build git marks snapshot
    extra: Visual Highlight Service, worker main

T3
    visto: diagnostics on line, diagnostics for editor line, rebuild diagnostics by line, rebuild diagnostics by line if changed
    extra: handle gutter marker click, handle editor mouse

T4
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  mismo objeto: scan shell output for linker errors
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → display text
  T1 → build all rows
  T2 → Visual Highlight Service
  T2 → worker main
  T3 → handle gutter marker click
  T3 → handle editor mouse
