### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input, handle ai console keys
Cerrado:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas truncadas 3050-3060, donde Escape invoca a ai clear pending insert si hay una inserción pendiente; el clic fuera no se captura explícitamente en este handler, pero el foco se gestiona vía Take Focus en eventos custom. El efecto de cancelación se ejecuta en clear pending insert y cancel current .
Abierto:
- clear pending insert
- has pending insert

### Trabajo 2
consulta: qué hace clear pending insert para limpiar el estado de la generación
keep: M8
leído: clear pending insert
Cerrado:
encontré el mecanismo: clear pending insert limpia el estado estableciendo pending insert false, reseteando insert anchor a su valor por defecto y llamando a wake true para notificar el cambio
Abierto:
- handle route

### Trabajo 3
consulta: qué hace cancel current para detener la generación activa
keep: M8
leído: cancel current, cancel all, clear pending insert, busy, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo, o ampliar otro polo de existencia (no el mismo disparo).
encontré el mecanismo: cancel current distingue dos ramas según el estado. Si hay una inserción pendiente y no está busy, limpia el estado de inserción clear pending insert y despierta. Si está busy generación activa, delega en cancel all, que a su vez llama a cancel level1 que setea el flag agent cancel para abortar el agente L1 y cancela las tareas pendientes tasks cancel . Finalmente, despierta el sistema.
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: ensure coding stem index ready, cancel all

T2
    visto: clear pending insert
    extra: cancel current, handle user input

T3
    visto: cancel current, cancel all, clear pending insert, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel level1
  T1=>T3  cancel all → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → ensure coding stem index ready
  T1 → Ai Controller
  T3 → Ai Controller
  T3 → Symbol Filter Runner
