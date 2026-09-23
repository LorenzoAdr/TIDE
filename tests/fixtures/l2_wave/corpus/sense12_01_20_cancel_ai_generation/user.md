Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=10  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=0  stems: search replace
owns: search replace
gap: state,trigger,effect
peek: search replace find unique span
M3  kind=caller  ov=6  stems: level2 session level2 session
owns: level2 session
nucleus: phase, err out
peek: level2 session reopen for followup, level2 session apply tool
port: M3=>M8 ai controller clear ai session -call-> ai controller clear
M4  kind=caller  ov=6  stems: file picker file picker
owns: file picker
nucleus: ctrl chord active
peek: file picker MakeFilePickerOverlay, file picker cancel ctrl chord
port: M4=>M6 application open quick file picker -call-> ai controller clear
M5  kind=caller  ov=0  stems: console panel console panel
owns: console panel
nucleus: b row
peek: console panel copy terminal selection, console panel terminal selected text
port: M5=>M6 console panel handle ai console keys -call-> ai controller clear
M6  kind=caller  ov=0  stems: level1 agent level1 agent
owns: level1 agent
nucleus: ctx
peek: level1 agent filter distilled ignore, level1 agent run
port: M6=>M8 ai controller cancel level1 -call-> workspace config load
M7  kind=chrome  ov=4  stems: editor panel editor panel
owns: editor panel
nucleus: scroll
peek: editor panel MakeEditorPanel, editor panel handle breadcrumb click
port: M7=>M6 level1 agent run -call-> editor text clear
M9  kind=hole  ov=6  stems: app session shell session raw pty screen app session
owns: app session
gap: no state
peek: raw pty screen skip escape, raw pty screen handle csi
peek-edge: raw pty screen skip escape -call-> raw pty screen handle csi
M10  kind=hole  ov=6  stems: gdb launcher thread safe queue git service gdb launcher
owns: gdb launcher
gap: no state
peek: git service unbind remote cancel, thread safe queue re

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=2  (owns+nucleus+peek+circuito; sin inspect gordo)
M2  kind=caller  ov=6
owns: ai trace
peek: ai trace ai trace escape
port: M2=>M3 level2 session json escape -call-> ai trace ai trace escape
M8  kind=latch  ov=6
owns: ai controller
nucleus: pending insert 
peek: ai controller begin insert at, ai controller cancel level1
port: M8=>M6 ai controller cancel level1 -call-> workspace config load
entre abiertas:
M2=>M8 ai controller handle route -call-> ai trace ai trace escape
hacia el resto:
M2=>M3 level2 session json escape -call-> ai trace ai trace escape
M3=>M8 ai controller clear ai session -call-> ai controller clear
M6=>M8 ai controller cancel level1 -call-> workspace config load
M8=>M6 ai controller cancel level1 -call-> workspace config load
## M2
kind=caller
owns: ai trace
nuclei:
anchors:
- G1: ai trace ai trace escape[q2]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
ports:
- M2=>M3 level2 session json escape -call-> ai trace ai trace escape
- M2=>M8 ai controller handle route -call-> ai trace ai trace escape
causal edges:
- level2 session json escape -call-> ai trace ai trace escape [port]
- ai controller handle route -call-> ai trace ai trace escape [port]
mini-cards:
- ai trace ai trace escape | std string ai trace escape(std string view s, std size t max len) | roles=query
· 53: out.reserve(std min(s.size(), max len) + 16);
· 54: for (std size t i = 0; i < s.size() && out.size() < max len; ++i) {
· 55: const unsigned char c = static cast<unsigned char>(s[i]);
· 57: out.push back('\\');
risks: no state nucleus
## M8
kind=latch
owns: ai controller
nuclei: C3(pending insert )
anchors:
- G1: ai controller cancel level1[q13]
- G2: ai controller cancel current[q7]
closure: 2/2 groups, hub-free=2
roles:
- writers: ai controller clear pending insert, ai controller begin insert at
- readers: ai controller cancel current
- controls: ai controller L752:if
mechanism:
- trigger: ai controller cancel current -enter ctrl-> ai controller L752:if if (pending insert  && !busy())
- state: ai controller clear pending insert -write(pending insert )-> pending insert 
- effect: ai controller cancel current -call-> ai controller wake
ports:
- M8=>M6 ai controller cancel level1 -call-> workspace config load
causal edges:
- ai controller clear pending insert -write(pending insert )-> pending insert  [state]
- ai controller cancel current -enter ctrl-> ai controller L752:if [trigger] if (pending insert  && !busy())
- ai controller cancel current -call-> ai controller wake [effect]
- ai controller cancel level1 -call-> workspace config load [port]
mini-cards:
- ai controller cancel level1 | void AiController cancel level1() | roles=query | hot=symptom edge
· 737: agent cancel .store(true);
· 738: if (agent busy .load()) {
· 739: append("L1: cancel solicitado…");
- ai controller cancel current | void AiController cancel current() | roles=query,ui | hot=early return,multi return,wake
· 753: clear pending insert();
· 755: wake(true);
· 762: cancel all();
· 763: wake(true);
- ai controller begin insert at | void AiController begin insert at(const AiInsertAnchor& anchor) | roles=mutator,ui | writes=layout.focus sync needed,insert anchor ,layout.console tabs.selected tab | reads=Console,anchor,kAi | hot=wake,symptom edge
## uncovered seeds
- q6 raw pty screen skip escape
- q9 git service unbind remote cancel
- q11 git service cancel remote
- q12 git command request git cancel
- q14 application cancel debug launch

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se limpia el archivo o se revierte la escritura cuando se cancela la generación de código
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1, begin insert at, tasks, agent cancel
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el archivo o se revierte la escritura cuando se cancela la generación de código
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
no encontré el mecanismo que limpia el archivo o revierte la escritura en disco al cancelar; lo leído muestra que la cancelación solo limpia estado en memoria pending insert, agent cancel y solicita cancelación a tareas agentes, sin lógica de revertir el buffer del editor ni restaurar el contenido del archivo.
Abierto:
- handle route
- handle user input
- tasks cancel
- agent cancel effect
- editor buffer revert

### Trabajo 2
consulta: dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → handle user input
  T1 → handle route

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se limpia el archivo o se revierte la escritura cuando se cancela la generación de código
Cerrado de T1:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
no encontré el mecanismo que limpia el archivo o revierte la escritura en disco al cancelar; lo leído muestra que la cancelación solo limpia estado en memoria pending insert, agent cancel y solicita cancelación a tareas agentes, sin lógica de revertir el buffer del editor ni restaurar el contenido del archivo.

T2 preguntó:
dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación
Cerrado de T2:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

Esta pregunta ya tiene respuesta (no hay objeto). No la relances. Cierra el claim si basta lo acumulado, o formula otra pregunta.

Legal ahora: explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

Esa pregunta ya tiene respuesta (no hay objeto). No la relances. Cierra el claim si basta lo acumulado, o formula otra pregunta. Ya se tiró: «dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación». Rechazado: dónde se captura la pulsación de Escape o el clic fuera para iniciar la cancelación de la generación
