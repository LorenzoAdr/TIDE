### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: console panel cpp, shell session cpp, scan shell output for linker errors
Cerrado:
leído: console panel cpp, shell session cpp, scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: gutter buffer line at row, line gutter marker, fold gutter marker, line diagnostic marker from map, git line changed
Cerrado:
leído: gutter buffer line at row, line gutter marker, fold gutter marker, line diagnostic marker from map, git line changed
Abierto:
- handle editor mouse
- handle fold gutter click


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: console panel cpp, shell session cpp, scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: gutter buffer line at row, line gutter marker, fold gutter marker, line diagnostic marker from map
    extra: handle editor mouse, Make Editor Panel

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T2 → handle editor mouse
  T2 → Make Editor Panel
