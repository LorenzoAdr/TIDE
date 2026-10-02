### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, Make Editor Panel
Cerrado:
encontré el objeto de la consulta; el margen se marca en el bucle de viewport lines dentro de Make Editor Panel, donde se asigna gutter marker y se usa line diagnostics para el sufijo y subrayado
Abierto:
- show diagnostic suffix on line

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, Make Editor Panel
    extra: Make Main Layout, run level1 async

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → run level1 async
  T2 → tick binary symbols panel
  T2 → has value
