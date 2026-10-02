### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: on closed
Cerrado:
leído: on closed
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la lista de archivos abiertos al cerrar el editor
keep: M1
leído: clear tabs, open tabs mru, workspace model hpp
Cerrado:
encontré dónde se guarda la lista: en los miembros vector Editor Tab tabs y string tab mru de la estructura Workspace Model declarados en workspace model . La función clear tabs confirma que estos son los contenedores que se vacían al cerrar.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda el estado de los paneles laterales al cerrar el editor
keep: M7
leído: workspace model hpp, clear tabs
Cerrado:
leído: workspace model hpp, clear tabs
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on closed
    extra: jump to selected, cancel ctrl chord

T2
    visto: clear tabs, open tabs mru, workspace model hpp
    extra: set welcome buffer, normalize path

T3
    visto: workspace model hpp, clear tabs
    extra: set welcome buffer

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: clear tabs
hacia el resto:
  T1 → jump to selected
  T1 → cancel ctrl chord
  T2 → set welcome buffer
  T2 → normalize path
  T3 → set welcome buffer
