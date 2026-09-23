### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1 M12
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Cerrado:
encontré el mecanismo de decisión: la línea roja marcador de error se resuelve como el carácter en line diagnostic marker from map 978 993, que es llamado por line gutter marker 1005 1017 . El dibujo real delega a la infraestructura de UI no leída que consume este carácter para renderizar el icono en el gutter.
Abierto:
- render gutter icon
- draw char in gutter

### Trabajo 2
consulta: dónde se analizan los errores de compilación en la salida de la consola para extraer la línea y el mensaje
keep: M4 M11
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para crear marcadores en el editor
keep: M1 M3
leído: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
Cerrado:
leído: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, handle gutter marker click
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T3
    visto: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
    extra: Make Editor Panel, line gutter marker

entre abiertas:
  T1=>T2  mismo objeto: rebuild diagnostics by line if changed
  T1=>T3  mismo objeto: line diagnostic marker from map
  T1=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T2=>T3  sin camino
hacia el resto:
  T1 → apply lsp diagnostics to panel
  T1 → push active file diagnostics from cache
  T2 → apply lsp diagnostics to panel
  T2 → push active file diagnostics from cache
  T3 → Make Editor Panel
  T3 → Make Main Layout
