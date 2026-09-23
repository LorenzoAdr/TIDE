### Trabajo 1
consulta: dónde se pintan los errores de compilación en el margen del editor
keep: M1 M11
leído: collect diagnostic decorations, apply decoration, Render Editor Line
Cerrado:
encontré el mecanismo de pintura: los errores se pintan en el cuerpo de la línea no en un margen gutter lateral mediante collect diagnostic decorations que genera decoraciones de tipo Diagnostic Error y apply decoration que aplica color al elemento. El flujo completo está anclado en Render Editor Line .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se configuran las decoraciones de diagnóstico para aparecer en el margen izquierdo del editor
keep: M1 M9
leído: collect diagnostic decorations, apply decoration, Render Editor Line
Cerrado:
leído: collect diagnostic decorations, apply decoration, Render Editor Line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se definen las decoraciones de diagnóstico para añadir un marcador en el margen izquierdo
keep: M1 M2
leído: line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostic severity color, Make Editor Panel
Cerrado:
encontré la definición del marcador en el margen izquierdo: line gutter marker 1005 decide el carácter o W delegando en line diagnostic marker from map, y diagnostic severity color 2084 define el color asociado a la severidad.
Abierto:
- handle editor mouse
- git line changed


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: collect diagnostic decorations, apply decoration, Render Editor Line
    extra: Make Editor Panel, make sticky overlay

T2
    visto: collect diagnostic decorations, apply decoration, Render Editor Line
    extra: Make Editor Panel, make sticky overlay

T3
    visto: line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostic severity color
    extra: Make Main Layout, git line changed

entre abiertas:
  T1=>T2  mismo objeto: collect diagnostic decorations
  T1=>T3  extra toca visto: Make Editor Panel
  T2=>T3  extra toca visto: Make Editor Panel
hacia el resto:
  T1 → make sticky overlay
  T1 → render rich line
  T2 → make sticky overlay
  T2 → render rich line
  T3 → Make Main Layout
  T3 → git line changed
