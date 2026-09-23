### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, clear pending insert, cancel current, handle user input, handle route, is cancel input, cancel all
Cerrado:
encontré el mecanismo: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if que invoca a clear pending insert para cancelar la inserción pendiente. El clic fuera no se captura explícitamente en este flujo; la cancelación por teclado es la vía principal documentada en el código leído.
Abierto:
- cancel level1

### Trabajo 2
consulta: dónde se invoca la cancelación del agente de nivel 1 para detener la generación
keep: M6 M8
leído: cancel level1, cancel current, handle user input, handle route
Cerrado:
encontré el objeto de la consulta: la cancelación del agente de nivel 1 se invoca en cancel all llamada a cancel level1, y cancel all se invoca desde cancel current cuando hay trabajo activo, y desde el destructor Ai Controller. cancel current se invoca desde handle user input vía is cancel input y handle route vía Cancel Agent .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo o se aborta la escritura para evitar dejar el código a medias tras cancelar
keep: M8
leído: clear pending insert, cancel current, handle user input, handle route
Cerrado:
encontré el mecanismo de limpieza: clear pending insert resetea pending insert e insert anchor y despierta el sistema; se invoca desde cancel current ruta de cancelación explícita y directamente en handle user input cuando se detecta un input de cancelación o se confirma la inserción. No se deja código a medias porque el estado pendiente se borra antes de cualquier escritura real.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, clear pending insert, cancel current, handle user input
    extra: has pending insert, ai tab active

T2
    visto: cancel level1, cancel current, handle user input, handle route
    extra: cancel all, Ai Controller

T3
    visto: clear pending insert, cancel current, handle user input, handle route
    extra: cancel all, run insert async

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T1=>T3  mismo objeto: clear pending insert
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → Ai Controller
  T2 → run insert async
  T3 → run insert async
  T3 → handle level2 harness
