### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, Make Editor Panel, show diagnostic suffix on line, diagnostic severity tr
Cerrado:
encontré el mecanismo: en Make Editor Panel dentro del bucle for se llama a diagnostics for editor line para obtener los errores de la línea i . Si show diagnostic suffix line retorna true, se asigna un gutter marker carácter como o W y un suffix ptr texto que se renderizan en el margen gutter del editor. La severidad se traduce vía diagnostic severity tr .
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para crear diagnósticos
keep: M7
leído: diagnostics for editor line, diagnostics on line, terminal selected text, select console word at, scan shell output for linker errors
Cerrado:
leído: diagnostics for editor line, diagnostics on line, terminal selected text, select console word at, scan shell output for linker errors
Abierto:
- handle editor mouse
- handle gutter marker click
- handle console panel mouse

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M7
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, Make Editor Panel, show diagnostic suffix on line
    extra: Make Main Layout, handle gutter marker click

T2
    visto: diagnostics for editor line, diagnostics on line, terminal selected text, select console word at
    extra: Make Main Layout, Make Editor Panel

T3
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
  T1=>T3  sin camino
  T2=>T3  mismo objeto: scan shell output for linker errors
hacia el resto:
  T1 → Make Main Layout
  T1 → handle gutter marker click
  T2 → Make Main Layout
  T2 → handle gutter marker click
  T3 → tick binary symbols panel
  T3 → has value
