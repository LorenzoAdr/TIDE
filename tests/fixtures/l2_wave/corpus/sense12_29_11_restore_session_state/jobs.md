### Trabajo 1
consulta: dónde se restauran los archivos abiertos y la posición del cursor al volver a abrir un proyecto
keep: M1 M3
leído: restore workspace session, open file, open file impl, open new tab from disk
Cerrado:
leído: restore workspace session, open file, open file impl, open new tab from disk
Abierto:
- handle navigation

### Trabajo 2
consulta: dónde se restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
keep: M2
leído: restore workspace session, open file impl
Cerrado:
leído: restore workspace session, open file impl
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda el estado de la sesión de trabajo para restaurar archivos, cursor y paneles
keep: M3
leído: restore workspace session, open file impl, open file, open new tab from disk
Cerrado:
leído: restore workspace session, open file impl, open file, open new tab from disk
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restore workspace session, open file, open file impl, open new tab from disk
    extra: Application, set workspace

T2
    visto: restore workspace session, open file impl
    extra: Application, set workspace

T3
    visto: restore workspace session, open file impl, open file, open new tab from disk
    extra: set workspace, Application

entre abiertas:
  T1=>T2  mismo objeto: restore workspace session
  T1=>T3  mismo objeto: restore workspace session
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → Application
  T1 → set workspace
  T2 → Application
  T2 → set workspace
  T3 → set workspace
  T3 → Application
