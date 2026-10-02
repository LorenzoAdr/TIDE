### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M2 M12
leído: collect diagnostic decorations, push active file diagnostics from cache, Render Editor Line, render rich line, apply decoration, decoration at
Cerrado:
encontré el mecanismo de marcado de errores: se marcan como decoraciones de línea subrayado color en el cuerpo del texto, no en el margen gutter. La función collect diagnostic decorations crea decoraciones del tipo Diagnostic Error Diagnostic Warning que apply decoration renderiza con underlined y color de error. No existe un mecanismo de iconos en el gutter para errores de compilación en este código; el gutter se usa para folds y git, pero no para diagnostics.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se definen los tipos de decoraciones de línea y si existe soporte para iconos o marcas en el margen gutter
keep: M4 M10
leído: configure glyphs, apply decoration, Render Editor Line, render rich line, decoration at, collect diagnostic decorations, editor render hpp
Cerrado:
encontré la definición de los tipos de decoraciones de línea en el enum Kind dentro de editor render leído en ola 5 . Respecto al soporte para iconos o marcas en el margen gutter: no existe tal soporte en el sistema de decoraciones de línea; las decoraciones apply decoration solo aplican estilos de texto color, fondo, negrita, subrayado sobre el contenido de la línea, y el gutter se gestiona por separado en helix dispatch sin integración de iconos de glyphs en las decoraciones de línea.
Abierto:
- handle general settings keys

### Trabajo 3
consulta: dónde se renderiza el margen izquierdo gutter del editor y qué elementos dibuja
keep: M1 M2
leído: Make Editor Panel, Render Editor Line, line gutter marker, fold gutter marker, helix gutter width
Cerrado:
leído: Make Editor Panel, Render Editor Line, line gutter marker, fold gutter marker, helix gutter width
Abierto:
- handle fold gutter click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: collect diagnostic decorations, push active file diagnostics from cache, Render Editor Line, render rich line
    extra: Make Editor Panel, make sticky overlay

T2
    visto: configure glyphs, apply decoration, Render Editor Line, render rich line
    extra: toggle option, Application

T3
    visto: Make Editor Panel, Render Editor Line, line gutter marker, fold gutter marker
    extra: Make Main Layout, ensure buffer

entre abiertas:
  T1=>T2  mismo objeto: collect diagnostic decorations
  T1=>T3  mismo objeto: Render Editor Line
  T1=>T3  Make Editor Panel → Render Editor Line → render rich line → collect diagnostic decorations
  T2=>T3  mismo objeto: Render Editor Line
  T2=>T3  Make Editor Panel → Render Editor Line → render rich line → apply decoration
hacia el resto:
  T1 → make sticky overlay
  T1 → apply lsp diagnostics to panel
  T2 → toggle option
  T2 → Application
  T3 → Make Main Layout
  T3 → ensure buffer
