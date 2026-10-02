### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, load file, open file at confirmed, open file impl
Cerrado:
leído: Cursor Pos, open file at impl, load file, open file at confirmed, open file impl
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: restore workspace session, open file
Cerrado:
encontré el mecanismo: restore workspace session lee load y itera session open tabs llamando a workspace open file path; el disparo es set workspace llamado desde el constructor Application o al cambiar workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2
leído: restore workspace session
Cerrado:
leído: restore workspace session
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, open file at impl, load file, open file at confirmed
    extra: cursor selection span on line, ensure cursors

T2
    visto: restore workspace session, open file
    extra: Application, set workspace

T3
    visto: restore workspace session
    extra: Application, set workspace

entre abiertas:
  T1=>T2  open file at impl → open file impl → open new tab from disk → open file
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → Application
  T2 → set workspace
  T3 → Application
  T3 → set workspace
