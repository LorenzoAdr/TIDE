### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al volver a abrir un proyecto
keep: M1 M4
leído: save workspace session, restore workspace session, save, flush active tab, app settings hpp, workspace model hpp
Cerrado:
leído: save workspace session, restore workspace session, save, flush active tab, app settings hpp, workspace model hpp
Abierto:
- run custom event drain
- run background generation

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo que ya estaba abierto
keep: M2 M3
leído: navigate editor to line, Make Editor Panel
Cerrado:
leído: navigate editor to line, Make Editor Panel
Abierto:
- handle editor mouse
- handle sticky scroll click

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M8 M9
leído: handle source panel event, Split Separator Vertical
Cerrado:
leído: handle source panel event, Split Separator Vertical
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, restore workspace session, save, flush active tab
    extra: run custom event drain, apply pending connection

T2
    visto: navigate editor to line, Make Editor Panel
    extra: handle sticky scroll click, handle editor mouse

T3
    visto: handle source panel event, Split Separator Vertical
    extra: Character, handle source scrollbar mouse

entre abiertas:
  T1=>T2  Make Editor Panel → drain visual highlight results → clear completion wake
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → handle sticky scroll click
  T2 → handle editor mouse
  T3 → Character
  T3 → handle source scrollbar mouse
