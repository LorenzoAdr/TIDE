### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: cancel level1, ai trace escape, clear hover if
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de Escape o clic fuera para cancelar la IA; lo leído son funciones de utilidad escape de strings, hover UI y el efecto de cancelación cancel level1, pero no el disparador UI
Abierto:
- handle problems scrollbar mouse
- Event handler global
- key escape handler
- click outside cancel

### Trabajo 2
consulta: dónde se limpia el archivo para evitar que quede a medias al cancelar la generación
keep: M6 M9
leído: cancel current, clear pending insert, cancel all, handle user input, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, handle user input, cancel level1
Abierto:
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, ai trace escape, clear hover if
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all, handle user input
    extra: handle route, Ai Controller

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T1 → handle problems scrollbar mouse
  T2 → handle route
  T2 → Ai Controller
