### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M7
leído: handle ai console keys, handle console panel mouse, cancel current, clear pending insert
Cerrado:
leído: handle ai console keys, handle console panel mouse, cancel current, clear pending insert
Abierto:
- handle user input
- handle route

### Trabajo 2
consulta: dónde se limpia el archivo o se revierten los cambios pendientes al cancelar la generación
keep: M10 M7
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo o se revierten los cambios pendientes al cancelar la generación; lo leído son banderas de cancelación agent cancel, pending insert y llamadas a tasks cancel, pero no hay código que reverta contenido de archivos ni limpie buffers de texto en el editor
Abierto:
- handle user input
- handle route
- tasks cancel
- agent cancel effect
- editor revert changes


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, cancel current, clear pending insert
    extra: has pending insert, ai tab active

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
