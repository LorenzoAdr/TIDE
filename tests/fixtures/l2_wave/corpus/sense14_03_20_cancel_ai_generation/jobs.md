### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para interrumpir la generación de la IA
keep: M1 M2
leído: cancel level1
Cerrado:
leído: cancel level1
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación de la IA
keep: M1
leído: clear pending insert, cancel current
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la limpieza del archivo parcialmente escrito; lo leído clear pending insert solo resetea flags en memoria pending insert, insert anchor y no toca el sistema de archivos
Abierto:
- handle route
- handle user input
- cancel all
- file write cleanup


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1
    extra: Ai Controller, cancel all

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  cancel current → cancel all → cancel level1
hacia el resto:
  T1 → Ai Controller
  T1 → cancel all
  T2 → handle user input
  T2 → handle route
