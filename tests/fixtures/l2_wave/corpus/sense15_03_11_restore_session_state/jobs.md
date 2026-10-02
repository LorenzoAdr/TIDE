### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close
Cerrado:
leído: close
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la lista de archivos abiertos al cerrar el editor
keep: M1 M5
leído: editor panel hpp, Make Editor Panel
Cerrado:
leído: editor panel hpp, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura el estado de la sesión al abrir un proyecto existente
keep: M6 M4
leído: restore workspace session, load, Application, set workspace
Cerrado:
encontré el mecanismo de restauración: restore workspace session es la función que restaura el estado llamada desde el constructor Application y desde set workspace, la cual invoca a load para leer el JSON y luego itera sobre open tabs y active tab path llamando a workspace open file .
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close
    extra: request start, open for pid

T2
    visto: editor panel hpp, Make Editor Panel
    extra: Make Main Layout, ensure buffer

T3
    visto: restore workspace session, load, Application, set workspace
    extra: open file, is regular file

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  restore workspace session → last launch program → open launch wizard → prepare connection wizard → exit debug mode
  T2=>T3  sin camino
hacia el resto:
  T1 → request start
  T1 → open for pid
  T2 → Make Main Layout
  T2 → ensure buffer
  T3 → open file
  T3 → is regular file
