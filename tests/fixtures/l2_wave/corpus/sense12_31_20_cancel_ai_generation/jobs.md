### Trabajo 1
consulta: dónde se captura Escape o clic fuera para cancelar la generación de la IA
keep: M8
leído: ai trace escape, handle route, handle user input, is cancel input, cancel current
Cerrado:
encontré el objeto de la consulta: la captura de Escape clic fuera para cancelar la generación de IA está en handle user input que delega en is cancel input detecta cancel cancel cancelar y llama a cancel current que invoca cancel all si hay trabajo activo . No hay evidencia de captura de clic fuera en el código leído; el mecanismo es puramente textual teclado.
Abierto:
- cancel all

### Trabajo 2
consulta: qué hace cancel all para limpiar el archivo a medias y detener la generación
keep: M8
leído: cancel current, cancel level1, cancel all, clear pending insert
Cerrado:
encontré el mecanismo: cancel all limpia el archivo a medias invocando clear pending insert que resetea pending insert e insert anchor y detiene la generación llamando a cancel level1 que setea agent cancel y tasks cancel
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se limpia el buffer del editor para deshacer los cambios parciales de la IA
keep: M8
leído: clear pending insert, cancel current, handle user input
Cerrado:
encontré el mecanismo de limpieza del buffer de inserción pendiente: clear pending insert resetea pending insert y insert anchor, y es invocado directamente desde handle user input cuando el input es un comando de cancelación o cuando se consume el input para ejecutar la inserción y desde cancel current
Abierto:
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, handle route, handle user input, is cancel input
    extra: handle level2 harness, background

T2
    visto: cancel current, cancel level1, cancel all, clear pending insert
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current, handle user input
    extra: handle route, cancel all

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  mismo objeto: handle user input
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle user input → handle route → cancel current → cancel all → cancel level1
hacia el resto:
  T1 → handle level2 harness
  T1 → background
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → run insert async
  T3 → deshabilitado
