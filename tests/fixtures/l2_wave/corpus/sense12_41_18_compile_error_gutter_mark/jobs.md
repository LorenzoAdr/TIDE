### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: clear editor line paint caches
Cerrado:
leído: clear editor line paint caches
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: editor panel cpp
Cerrado:
leído: editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: editor panel cpp
Cerrado:
leído: editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
Cerrado:
leído: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: clear editor line paint caches

T2
    visto: editor panel cpp, clear editor line paint caches

T3
    visto: editor panel cpp

T4
    visto: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
    extra: has value, request binary symbols panel

entre abiertas:
  T1=>T2  mismo objeto: clear editor line paint caches
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: editor panel cpp
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T4 → has value
  T4 → request binary symbols panel
