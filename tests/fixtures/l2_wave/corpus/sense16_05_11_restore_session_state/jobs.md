### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: session pack needles, session path, load, Application, save, set workspace, save workspace session
Cerrado:
encontré el mecanismo de guardado y restauración del estado del editor tabs abiertos y tab activo al cambiar de proyecto. El estado se persiste en session md ruta derivada de workspace root mediante save y se restaura mediante load . El disparo de guardado ocurre en set workspace antes de limpiar tabs y en eventos de cierre; la restauración se invoca en el constructor Application tras detectar el workspace.
Abierto:
- run custom event drain
- Event (restauración explícita post-cierre)

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor en el archivo al cerrar y abrir un proyecto
keep: M1 M10
leído: save, load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor en el archivo al cerrar y abrir un proyecto; lo leído en load solo serializa open tabs y active tab path, sin cursor position ni line column
Abierto:
- save state
- restore state
- save cursor positions

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir un proyecto
keep: M7 M10
leído: save workspace session
Cerrado:
leído: save workspace session
Abierto:
- run custom event drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: session pack needles, session path, load, Application
    extra: is plan stats telemetry token, looks like code ident

T2
    visto: save, load
    extra: session path, create directories

T3
    visto: save workspace session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  mismo objeto: load
  T1=>T3  mismo objeto: save workspace session
  T2=>T3  sin camino
hacia el resto:
  T1 → is plan stats telemetry token
  T1 → looks like code ident
  T2 → create directories
  T2 → write terminal init script
  T3 → run custom event drain
  T3 → apply pending connection
