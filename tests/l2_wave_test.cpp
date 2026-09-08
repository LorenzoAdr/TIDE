#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_wave.hpp"

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  using tuide::WaveDo;
  using tuide::WaveHit;
  using tuide::WaveOla;
  using tuide::WaveOps;
  using tuide::WavePeekNeighbors;
  using tuide::WaveState;
  using tuide::wave_apply;
  using tuide::wave_campo_match;
  using tuide::wave_check_barriers;
  using tuide::wave_find_hit;
  using tuide::wave_needle_search_keys;
  using tuide::wave_needle_stem_hint;
  using tuide::wave_parse_ola;

  {
    const auto bad = wave_parse_ola("no json here");
    expect(!bad.ok, "raw sin JSON");
    expect(bad.error.find("JSON") != std::string::npos || bad.error.find("objeto") != std::string::npos,
           "error objeto JSON");
  }
  {
    const auto bad = wave_parse_ola(R"({"action":"ola_v1","do":"needles","needles":["join"],"why":"x"})");
    expect(!bad.ok, "why corto");
  }
  {
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["set_busy_spinner","join"],"campo":"busy_strip","why":"buscar el LED del spinner"})");
    expect(ola.ok, "parse needles ok");
    expect(ola.do_kind == WaveDo::Needles, "do needles");
    expect(ola.needles.size() == 2, "2 needles");
    expect(ola.campo == "busy_strip", "campo");
  }
  {
    const auto fol = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"M1","why":"quién llama al latch desde cualquier stem"})");
    expect(fol.ok, "parse follow");
    expect(fol.do_kind == WaveDo::Follow, "do follow");
    expect(fol.follows.size() == 1 && fol.follows[0] == "M1", "follows M1");
    const auto mixed_f = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peeks":["M1"],"follows":["M1"],"why":"cuerpo y flujo causal del latch"})");
    expect(mixed_f.ok, "parse peek+follow");
    expect(mixed_f.do_kind == WaveDo::Tanda, "peek+follow vira tanda");
    const auto v3 = wave_parse_ola(
        R"({"action":"ola_v3","do":"tanda","peeks":["M1"],"needles":["end_thinking"],"why":"cuerpo del latch y aguja del apagado"})");
    expect(v3.ok, "parse ola_v3");
    expect(v3.do_kind == WaveDo::Tanda, "ola_v3 tanda");
    const auto ent = wave_parse_ola(
        R"({"action":"ola_v1","do":"entre","from":"begin_thinking","to":"end_thinking","why":"camino del encendido al apagado"})");
    expect(ent.ok, "parse entre");
    expect(ent.do_kind == WaveDo::Entre, "do entre");
    expect(ent.from == "begin_thinking" && ent.to == "end_thinking", "from to");
    const auto ent2 = wave_parse_ola(
        R"({"action":"ola_v1","do":"path","entre":["M1","M7"],"why":"camino entre las dos zonas abiertas"})");
    expect(ent2.ok, "parse path alias");
    expect(ent2.do_kind == WaveDo::Entre, "path vira entre");
    expect(ent2.from == "M1" && ent2.to == "M7", "entre array");
    const auto inb = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"begin_thinking","needles":["catch","throw"],"why":"¿el caller traga la excepción?"})");
    expect(inb.ok, "parse in needles");
    expect(inb.in_locus == "begin_thinking", "in locus");
    expect(inb.needles.size() == 2, "in 2 needles");
    const auto in_empty = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"begin_thinking","why":"grep sin agujas no vale"})");
    expect(!in_empty.ok, "in sin needles");
    const auto cerca = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["cancel work","child dead"],"in":[],"why":"vi el arranque; busco quién cancela"})");
    expect(cerca.ok, "parse cerca");
    expect(cerca.do_kind == WaveDo::Cerca, "do cerca");
    expect(cerca.needles.size() == 2, "cerca 2 needles");
    expect(cerca.in_scopes.empty(), "cerca in vacío = inmediaciones");
    expect(cerca.in_locus.empty(), "cerca no usa in_locus");
    const auto cerca_in = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["process dead"],"in":["src/pkg"],"why":"busco detección de caída del hijo"})");
    expect(cerca_in.ok, "parse cerca in stem");
    expect(cerca_in.in_scopes.size() == 1 && cerca_in.in_scopes[0] == "src/pkg", "cerca in array");
    const auto cerca_frase = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["necesito que si el proceso se cae lo reinicie"],"why":"frase no vale como concepto"})");
    expect(!cerca_frase.ok, "cerca rechaza frase");
    expect(!tuide::wave_cerca_concept_ok("restart_lsp_for_workspace.cpp"), "cerca no path");
    expect(tuide::wave_cerca_concept_ok("child dead"), "cerca concepto ok");
    expect(tuide::wave_cerca_query({"process dead", "restart after fail"}) ==
               "process dead restart after fail",
           "cerca query join");
  }
  {
    const auto guion = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","qué se apaga","qué pasa con el archivo ya escrito"]})");
    expect(guion.ok, "parse guion");
    expect(guion.do_kind == WaveDo::Guion, "do guion");
    expect(guion.papeles.size() == 3, "3 papeles");
    expect(guion.why.empty(), "guion sin why");
    const auto uno = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta"]})");
    expect(!uno.ok, "guion rechaza 1 papel");
    const auto path = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","src/ai/ai_controller.cpp"]})");
    expect(!path.ok, "guion rechaza path");
    const auto zona = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","keep M1 latch"]})");
    expect(!zona.ok, "guion rechaza M*");
    expect(tuide::wave_guion_papel_ok("quién aborta"), "papel corto ok");
    expect(tuide::wave_guion_papel_ok("qué pasa con el archivo a medias si se cancela"),
           "papel con sentido entero");
    expect(!tuide::wave_guion_papel_ok("cancel_current"), "papel id no");
    const auto cinco = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta la generación","Escape o clic fuera","qué se apaga","archivo a medias","si el hijo murió"]})");
    expect(cinco.ok, "guion admite 5 papeles");
    const auto indep = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","papel":2,"why":"ciclo aparte para esa pregunta del guion"})");
    expect(indep.ok, "parse independiente");
    expect(indep.do_kind == tuide::WaveDo::Independiente, "do independiente");
    expect(indep.papel == 2, "papel 2");
    const auto indep0 = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","why":"ciclo aparte para esa pregunta del guion"})");
    expect(!indep0.ok, "independiente sin índice");
    const auto do_dash = wave_parse_ola(
        R"({"action":"ola_v1","do":"-","papeles":["quién aborta","qué se apaga","qué pasa con el archivo ya escrito"]})");
    expect(do_dash.ok, "guion recobra do inválido");
    expect(do_dash.do_kind == WaveDo::Guion, "do - es guion");
    expect(do_dash.papeles.size() == 3, "papeles tras do -");
  }
  {
    const auto truncated = wave_parse_ola("{`.` Single JSON format specified.\nThe atlas lists M1.");
    expect(!truncated.ok, "CoT truncado sin ola");
    const std::string cot =
        "{`.` Single JSON format specified.\n"
        "The atlas lists zones {M1} and {M7}.\n"
        R"({"action":"ola_v1","do":"juicio","keep":["M1","M7"],"why":"latch y caller cubren el objeto"})";
    const auto from_cot = wave_parse_ola(cot);
    expect(from_cot.ok, "JSON tras CoT con llaves sueltas");
    expect(from_cot.do_kind == WaveDo::Juicio, "juicio tras CoT");
    expect(from_cot.keep.size() == 2, "keep M1 M7");
    const auto think = wave_parse_ola(
        "<think>\n{`.` recito el atlas\n</think>\n"
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"why":"esta zona cubre el objeto"})");
    expect(think.ok, "JSON tras </think>");
    expect(think.keep.size() == 1 && think.keep[0] == "M1", "keep tras think");
    const auto fenced = wave_parse_ola(
        "```json\n"
        R"({"action":"ola_v1","do":"needles","needles":["start_job"],"why":"cazar el arranque del objeto"})"
        "\n```\n");
    expect(fenced.ok, "JSON en fence markdown");
    const auto last_wins = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M3"],"why":"esta zona cubre el objeto de la consulta"} )"
        "\nprosa\n"
        R"({"action":"ola_v1","do":"juicio","keep":["M1","M7"],"why":"latch y caller del objeto"})");
    expect(last_wins.ok, "último ola_v gana");
    expect(last_wins.keep.size() == 2, "no se queda la plantilla");
  }
  {
    expect(wave_needle_stem_hint("busy_strip::clear") == "busy_strip", "stem hint");
    expect(wave_needle_stem_hint("clear_busy_if").empty(), "sin ::");
    const auto keys = wave_needle_search_keys("busy_strip::clear");
    expect(keys.size() == 2, "keys qualified");
    expect(keys[0] == "busy_strip::clear", "key full");
    expect(keys[1] == "clear", "key symbol");
  }
  {
    const auto names = tuide::wave_extract_call_names(
        "void end_thinking() { clear_busy_if(layout); wake(true); }");
    expect(!names.empty(), "extract calls");
    bool saw = false;
    for (const auto& n : names) {
      if (n == "clear_busy_if") {
        saw = true;
      }
    }
    expect(saw, "extract clear_busy_if");
  }

  WaveHit latch;
  latch.id = "fn:src/ui/busy_strip.cpp:set_busy_spinner";
  latch.path = "src/ui/busy_strip.cpp";
  latch.symbol = "set_busy_spinner";
  latch.stem = "busy_strip";
  latch.kind = "fn";
  latch.needle = "set_busy_spinner";
  WaveHit clearer;
  clearer.id = "fn:src/ui/busy_strip.cpp:clear_busy_if";
  clearer.path = "src/ui/busy_strip.cpp";
  clearer.symbol = "clear_busy_if";
  clearer.stem = "busy_strip";
  clearer.kind = "fn";
  clearer.needle = "busy_strip::clear";
  WaveHit other_clear;
  other_clear.id = "fn:src/ui/console_panel.cpp:clear";
  other_clear.path = "src/ui/console_panel.cpp";
  other_clear.symbol = "clear";
  other_clear.stem = "console_panel";
  other_clear.kind = "fn";
  other_clear.needle = "clear";
  WaveHit chrome;
  chrome.id = "fn:src/ui/console_panel.cpp:paint";
  chrome.path = "src/ui/console_panel.cpp";
  chrome.symbol = "paint";
  chrome.stem = "console_panel";
  chrome.kind = "fn";
  chrome.needle = "paint";

  expect(wave_campo_match(latch, ""), "campo vacío");
  expect(wave_campo_match(latch, "busy_strip"), "campo stem");
  expect(wave_campo_match(latch, "ui/busy"), "campo path");
  expect(!wave_campo_match(latch, "console_panel"), "campo otro stem");

  WaveState st;
  st.prompt = "el spinner se queda infinito";
  wave_merge_hits(&st, {latch, chrome, latch});
  expect(st.candidatas.size() == 2, "merge dedup");

  {
    std::string err;
    WaveOla juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["set_busy_spinner"],"drop":["paint"],"why":"el latch es el indicador visible"})");
    expect(juicio.ok, "parse juicio");
    expect(wave_check_barriers(juicio, st, &err), "juicio con candidatas");
  }
  {
    WaveState empty;
    std::string err;
    WaveOla juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["x"],"why":"todavía no hay hits"})");
    expect(juicio.ok, "parse juicio vacío");
    expect(!wave_check_barriers(juicio, empty, &err), "barrera sin candidatas");
    expect(err.find("candidatas") != std::string::npos, "msg candidatas");
  }
  {
    std::string err;
    WaveOla peek = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"no_existe","why":"leer un símbolo inventado"})");
    expect(peek.ok, "parse peek");
    expect(!wave_check_barriers(peek, st, &err), "peek id desconocido");
  }

  WaveOps ops;
  ops.search_needle = [&](const std::string& needle, const std::string&) {
    if (needle == "set_busy_spinner") {
      return std::vector<WaveHit>{latch};
    }
    if (needle == "paint") {
      return std::vector<WaveHit>{chrome};
    }
    if (needle == "clear" || needle == "busy_strip::clear" || needle == "clear_busy" ||
        needle == "busy_strip::clear_busy") {
      return std::vector<WaveHit>{clearer, other_clear};
    }
    if (needle.find("end_thinking") != std::string::npos) {
      WaveHit e;
      e.id = "fn:src/ai/ai_controller.cpp:end_thinking";
      e.path = "src/ai/ai_controller.cpp";
      e.symbol = "end_thinking";
      e.stem = "ai_controller";
      e.needle = needle;
      return std::vector<WaveHit>{e};
    }
    if (needle == "clear_busy_spinner") {
      return std::vector<WaveHit>{};
    }
    return std::vector<WaveHit>{};
  };
  ops.peek_code = [&](const std::string& peek, std::string* text, std::string* err) {
    if (text == nullptr) {
      if (err) {
        *err = "text nulo";
      }
      return false;
    }
    if (peek.find("begin_thinking") != std::string::npos) {
      *text =
          "src/ai/ai_controller.cpp:begin_thinking\nvoid begin_thinking() { "
          "set_busy_spinner(layout, AiThinking); }";
      return true;
    }
    if (peek.find("end_thinking") != std::string::npos) {
      *text = "src/ai/ai_controller.cpp:end_thinking\nvoid end_thinking() { clear_busy_if(layout); }";
      return true;
    }
    if (peek.find("clear_busy") != std::string::npos) {
      *text = "src/ui/busy_strip.cpp:clear_busy_if\nvoid clear_busy_if() { ticker.join(); }";
      return true;
    }
    if (peek.find("busy_strip.hpp") != std::string::npos) {
      *text =
          "src/ui/busy_strip.hpp\nvoid set_busy_spinner(MainLayoutState*, BusyActivity, "
          "std::string_view);\nvoid clear_busy_if(MainLayoutState* layout);";
      return true;
    }
    if (peek.find("set_busy_spinner") == std::string::npos && peek != "M1") {
      if (err) {
        *err = "peek desconocido";
      }
      return false;
    }
    *text =
        "src/ui/busy_strip.cpp:set_busy_spinner\nvoid set_busy_spinner(bool on) { "
        "clear_busy_if(layout); busy_ = on; }";
    return true;
  };

  WaveState run;
  run.prompt = st.prompt;
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["set_busy_spinner"],"why":"cazar el LED del spinner"})");
    expect(wave_apply(&run, ola, ops, &err), "apply needles");
    expect(run.wave_n == 1, "wave_n 1");
    expect(wave_find_hit(run.candidatas, "set_busy_spinner") != nullptr, "hit spinner");
    expect(run.needles_log.size() == 1, "log 1 orden");
    expect(run.needles_log[0].needle == "set_busy_spinner", "log needle");
    expect(run.needles_log[0].hits == 1, "log hits");
    expect(run.needles_log[0].added == 1, "log added");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["clear_busy_spinner","busy_strip::clear"],"why":"buscar quién apaga el LED"})");
    expect(wave_apply(&run, ola, ops, &err), "apply needles qualified");
    expect(run.needles_log.size() == 3, "log acumula órdenes");
    expect(run.needles_log[1].needle == "clear_busy_spinner", "cero se anota");
    expect(run.needles_log[1].hits == 0, "cero hits");
    expect(run.needles_log[1].added == 0, "cero added");
    expect(run.needles_log[2].needle == "busy_strip::clear", "qualified");
    expect(run.needles_log[2].hits == 1, "filtra stem busy_strip");
    expect(wave_find_hit(run.candidatas, "clear_busy_if") != nullptr, "clear_busy_if vía ::");
    expect(wave_find_hit(run.candidatas, "console_panel.cpp:clear") == nullptr,
           "no cuela clear de otro stem");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["set_busy_spinner","clear_busy_spinner"],"why":"repetir agujas ya tiradas no avanza"})");
    expect(!wave_apply(&run, ola, ops, &err), "no repetir needles");
    expect(err.find("ya tirados") != std::string::npos, "msg ya tirados");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["ai_controller::set_busy_spinner"],"why":"la misma aguja con stem no es otra búsqueda"})");
    expect(!wave_apply(&run, ola, ops, &err), "needle alias stem::");
    expect(err.find("ya tirados") != std::string::npos, "msg alias ya tirados");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["set_busy_spinner"],"why":"es el indicador más bajo"})");
    expect(wave_apply(&run, ola, ops, &err), "apply juicio");
    expect(!run.zonas.empty() && run.zonas[0].verdict == "keep", "zona keep");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"ver el cuerpo del latch"})");
    expect(wave_apply(&run, ola, ops, &err), "apply peek");
    expect(run.notas.find("busy_") != std::string::npos, "nota peek");
    expect(run.notas.find("```cpp") != std::string::npos, "peek fenced cpp");
    expect(run.notas.find("### peek `set_busy_spinner`") != std::string::npos, "peek heading");
    expect(!run.mencionados.empty(), "peek extrae llamadas");
    bool saw_clear = false;
    for (const auto& m : run.mencionados) {
      if (m == "clear_busy_if") {
        saw_clear = true;
      }
    }
    expect(saw_clear, "mencionó clear_busy_if");
    WaveOla peek_mention = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"clear_busy_if","why":"el cuerpo nombra al clearer"})");
    expect(wave_check_barriers(peek_mention, run, &err), "peek mención del cuerpo");
    expect(!run.olas_log.empty(), "diario olas");
    const auto again = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"releer el mismo cuerpo no aporta"})");
    expect(!wave_apply(&run, again, ops, &err), "no re-peek");
    expect(err.find("ya leído") != std::string::npos, "msg peek ya leído");
    const auto alias = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"busy_strip::set_busy_spinner","why":"mismo símbolo con stem no es otro peek"})");
    expect(!wave_apply(&run, alias, ops, &err), "no re-peek alias");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerrar","why":"el LED está en busy_strip::set_busy_spinner"})");
    expect(wave_apply(&run, ola, ops, &err), "apply cerrar");
    expect(run.done, "done");
    expect(run.cierre.find("busy_strip") != std::string::npos, "cierre");
  }
  {
    WaveState early;
    early.prompt = "spinner infinito";
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerrar","why":"el latch vive en set_busy_spinner; el OFF del chat no está anclado"})");
    expect(wave_apply(&early, ola, ops, &err), "cerrar sin circuito ON/OFF");
    expect(early.done, "done aunque Circuito vacío");
    expect(early.cierre.find("set_busy_spinner") != std::string::npos, "cierre del piloto");
  }
  {
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["join"],"why":"otro grep tras cerrar no debe pasar"})");
    expect(!wave_apply(&run, ola, ops, &err), "no needles tras cerrar");
  }

  const std::string nb = wave_notebook_markdown(run);
  expect(nb.find("Candidatas") != std::string::npos, "notebook candidatas");
  expect(nb.find("## Diario") != std::string::npos, "notebook diario");
  expect(nb.find("## Needles") != std::string::npos, "notebook needles");
  expect(nb.find("clear_busy_spinner") != std::string::npos, "notebook orden 0");
  expect(nb.find("sin nodo") != std::string::npos, "notebook cero hits");
  expect(nb.find("clear_busy_if") != std::string::npos, "notebook id hallado");
  expect(nb.find("Cierre") != std::string::npos, "notebook cierre");

  {
    WaveState seeded;
    seeded.prompt = "spinner infinito";
    nlohmann::json payload = nlohmann::json::parse(R"({
      "zones": [
        {
          "id": "M1",
          "kind": "latch",
          "core_stems": ["busy_strip"],
          "anchors": [[{"target": "src/ui/busy_strip.cpp:set_busy_spinner", "stem": "busy_strip"}]],
          "representatives": [
            {"target": "src/ui/busy_strip.cpp:halt_busy_strip", "stem": "busy_strip"}
          ],
          "roles": {
            "writers": [
              {"target": "src/ui/busy_strip.cpp:clear_busy", "stem": "busy_strip", "kind": "fn"}
            ]
          }
        },
        {
          "id": "M2",
          "kind": "chrome",
          "core_stems": ["console_panel"],
          "anchors": [[{"target": "src/ui/console_panel.cpp:paint", "stem": "console_panel"}]]
        }
      ]
    })");
    expect(tuide::wave_seed_from_atlas(&seeded, payload) == 2, "seed 2 zonas");
    expect(tuide::wave_find_hit(seeded.candidatas, "M1") != nullptr, "seed M1");
    expect(tuide::wave_find_hit(seeded.candidatas, "set_busy_spinner") != nullptr, "seed peek symbol");
    expect(seeded.atlas_md.find("hipótesis") != std::string::npos, "atlas hipótesis");
    expect(seeded.atlas_md.find("busy_strip.hpp") != std::string::npos, "atlas lista header");
    expect(tuide::wave_find_hit(seeded.candidatas, "src/ui/busy_strip.hpp") != nullptr,
           "header peekable");
    expect(tuide::wave_needs_guion(seeded), "guion en ola 0");
    expect(!tuide::wave_needs_cover(seeded), "cover espera guion");
    std::string err;
    const auto guion = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién pinta el LED","qué lo apaga"]})");
    expect(guion.ok, "parse guion atlas");
    expect(wave_apply(&seeded, guion, ops, &err), "apply guion");
    expect(!tuide::wave_needs_guion(seeded), "tras guion no otra");
    expect(tuide::wave_needs_cover(seeded), "cover tras guion");
    const auto seed_nb = tuide::wave_notebook_markdown(seeded);
    expect(seed_nb.find("## Guion") != std::string::npos, "notebook guion");
    expect(seed_nb.find("## Estrategia") != std::string::npos, "notebook estrategia");
    expect(seed_nb.find("quién pinta el LED") != std::string::npos, "notebook papel");
    expect(seed_nb.find("qué tendrías que poder explicar") != std::string::npos, "notebook contrato guion");
    expect(seed_nb.find("files:") != std::string::npos, "notebook files");
    expect(seed_nb.find("busy_strip.hpp") != std::string::npos, "notebook header");
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M2"],"why":"el latch es el LED, chrome no"})");
    expect(wave_apply(&seeded, juicio, ops, &err), "juicio sobre atlas sin needles");
    expect(!tuide::wave_needs_cover(seeded), "tras juicio no cover");
    tuide::wave_retain_atlas_ids(&seeded, {"M1"});
    expect(tuide::wave_find_hit(seeded.candidatas, "M1") != nullptr, "retain M1");
    expect(tuide::wave_find_hit(seeded.candidatas, "M2") == nullptr, "drop atlas M2");
    expect(tuide::wave_find_hit(seeded.atlas_seed, "M2") != nullptr, "retain no borra atlas_seed");
    const auto baby = tuide::wave_independiente_child(seeded, "qué tecla cancela");
    expect(baby.independiente_leaf, "hijo leaf");
    expect(baby.papeles.size() == 1 && baby.prompt == "qué tecla cancela", "hijo un papel");
    expect(tuide::wave_find_hit(baby.candidatas, "M2") != nullptr, "hijo atlas completo M2");
    expect(tuide::wave_find_hit(baby.candidatas, "M1") != nullptr, "hijo atlas completo M1");
    expect(baby.peeks_done.empty() && baby.notas.empty() && baby.opened_ids.empty(),
           "hijo sin pack del padre");
    expect(tuide::wave_needs_cover(baby), "hijo cover fresco");
    expect(tuide::wave_find_hit(seeded.candidatas, "M2") == nullptr, "padre sigue recortado");
    {
      WaveState grown = baby;
      grown.opened_ids = {"M2"};
      WaveState back = seeded;
      tuide::wave_merge_independiente(&back, grown, 1);
      expect(tuide::wave_find_hit(back.candidatas, "M2") != nullptr, "merge trae zona que el hijo abrió");
    }
    expect(tuide::wave_ingest_zone_symbols(&seeded, payload, {"M1"}) >= 2, "ingest ficha symbols");
    expect(tuide::wave_find_hit(seeded.candidatas, "busy_strip::clear_busy") != nullptr,
           "writer clear_busy peekable");
    expect(tuide::wave_find_hit(seeded.candidatas, "halt_busy_strip") != nullptr,
           "rep halt_busy_strip peekable");
    const std::string after = tuide::wave_notebook_markdown(seeded);
    expect(after.find("via=ficha") != std::string::npos, "notebook símbolos ficha");
    seeded.opened_md = "M1  kind=latch\n    owns: busy_strip\n";
    const auto peek = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"M1","why":"leer el cuerpo del latch"})");
    expect(wave_check_barriers(peek, seeded, &err), "peek M1 tras seed");
    const auto peek_hdr = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"src/ui/busy_strip.hpp","why":"leer la API del latch"})");
    expect(peek_hdr.ok, "parse peek header");
    expect(wave_apply(&seeded, peek_hdr, ops, &err), "apply peek header");
    expect(seeded.notas.find("busy_strip.hpp") != std::string::npos, "nota header");
    const std::string snb = wave_notebook_markdown(seeded);
    expect(snb.find("## Atlas") != std::string::npos, "notebook atlas");
    expect(snb.find("## Abiertos") != std::string::npos, "notebook abiertos");
  }

  {
    WaveState cards;
    cards.prompt = "spinner infinito";
    cards.opened_ids = {"M1", "M7"};
    cards.opened_md =
        "# pilot_opened_v1\nM1  kind=latch\n\n# causal_judge_v1\n## M1\nkind=latch\n"
        "owns: busy_strip\n";
    cards.atlas_md =
        "M1  kind=latch  ov=7\n    owns: busy_strip\n\n"
        "M2  kind=chrome  ov=5\n    owns: console_panel\n";
    const auto rest = tuide::wave_atlas_rest_markdown(cards.atlas_md, cards.opened_ids);
    expect(rest.find("M2") != std::string::npos, "resto M2");
    expect(rest.find("M1  kind") == std::string::npos, "resto sin keep");
    const auto work = tuide::wave_work_markdown(cards);
    expect(work.find("## M1") != std::string::npos, "trabajo inspect keep");
    expect(work.find("## Atlas (resto)") != std::string::npos, "trabajo atlas resto");
    expect(work.find("console_panel") != std::string::npos, "resto chrome");
  }

  {
    WaveState batch;
    batch.prompt = "spinner infinito";
    WaveHit m1;
    m1.id = "M1";
    m1.path = "src/ui/busy_strip.cpp";
    m1.symbol = "set_busy_spinner";
    m1.stem = "busy_strip";
    m1.needle = "atlas";
    m1.files = {"src/ui/busy_strip.cpp", "src/ui/busy_strip.hpp"};
    wave_merge_hits(&batch, {m1});
    std::string err;
    const auto tanda = wave_parse_ola(
        R"({"action":"ola_v1","do":"tanda","peeks":["M1","src/ui/busy_strip.hpp"],"needles":["set_busy_spinner"],"why":"leer latch y header y cazar el LED"})");
    expect(tanda.ok, "parse tanda");
    expect(tanda.do_kind == WaveDo::Tanda, "do tanda");
    expect(tanda.peeks.size() == 2, "tanda 2 peeks");
    expect(wave_apply(&batch, tanda, ops, &err), "apply tanda");
    expect(batch.wave_n == 1, "tanda una ola");
    expect(batch.needles_log.size() == 1, "tanda needles");
    expect(batch.notas.find("set_busy_spinner") != std::string::npos, "tanda peek cuerpo");
    expect(batch.notas.find("busy_strip.hpp") != std::string::npos, "tanda peek header");
    const std::string bwork = tuide::wave_work_markdown(batch);
    expect(bwork.find("### peek `M1`") != std::string::npos, "trabajo retiene peek M1");
    expect(bwork.find("busy_strip.hpp") != std::string::npos, "trabajo retiene peek header");
    expect(bwork.find("## Peeks") != std::string::npos, "trabajo sección peeks");
    const auto mixed = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["paint"],"peek":"M1","why":"grep chrome y releer el latch"})");
    expect(mixed.ok, "parse mixed");
    expect(mixed.do_kind == WaveDo::Tanda, "needles+peek vira tanda");
  }

  {
    expect(wave_find_hit({clearer}, "busy_strip::clear_busy") != nullptr,
           "stem::prefijo halla clear_busy_if");
    WaveState t2;
    t2.prompt = "spinner infinito";
    WaveHit m1;
    m1.id = "M1";
    m1.path = "src/ui/busy_strip.cpp";
    m1.symbol = "set_busy_spinner";
    m1.stem = "busy_strip";
    m1.needle = "atlas";
    wave_merge_hits(&t2, {m1});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"tanda","peeks":["busy_strip::clear_busy","ai_controller::end_thinking"],"needles":["end_thinking","halt_busy_strip"],"why":"ver si clear_busy o end_thinking detienen el spinner"})");
    expect(ola.ok, "parse tanda qualified");
    expect(wave_check_barriers(ola, t2, &err), "tanda no exige peek ya en candidatas");
    expect(wave_apply(&t2, ola, ops, &err), "apply tanda qualified");
    expect(t2.wave_n == 1, "tanda qualified una ola");
    expect(!t2.needles_log.empty(), "tanda inyecta needles");
    expect(t2.notas.find("end_thinking") != std::string::npos ||
               t2.notas.find("clear_busy") != std::string::npos,
           "tanda inyecta peek al cuaderno");
    expect(wave_find_hit(t2.candidatas, "end_thinking") != nullptr, "needle end_thinking en candidatas");
  }

  {
    WaveState t_in;
    t_in.prompt = "spinner infinito";
    WaveHit m7;
    m7.id = "M7";
    m7.path = "src/ai/ai_controller.cpp";
    m7.symbol = "handle_user_input";
    m7.stem = "ai_controller";
    m7.needle = "atlas";
    WaveHit cb;
    cb.id = "fn:src/ui/busy_strip.cpp:clear_busy";
    cb.path = "src/ui/busy_strip.cpp";
    cb.symbol = "clear_busy";
    cb.stem = "busy_strip";
    cb.kind = "fn";
    wave_merge_hits(&t_in, {m7, cb});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"tanda","in":"stem::ai_controller","needles":["end_thinking","clear_busy"],"peeks":["busy_strip::clear_busy"],"why":"grep en el stem y leer el apagado"})");
    expect(ola.ok, "parse tanda in stem");
    expect(wave_apply(&t_in, ola, ops, &err), "tanda in stem no tumba peek");
    expect(t_in.campo.empty(), "in stem:: no vira campo");
    expect(t_in.notas.find("### peek") != std::string::npos, "peek inyectado pese a in stem");
    expect(t_in.notas.find("clear_busy") != std::string::npos, "cuerpo clear_busy en notas");
    expect(t_in.last_error.empty(), "sin last_error si el peek aplicó");
  }

  {
    WaveState stem_in;
    stem_in.prompt = "spinner infinito";
    wave_merge_hits(&stem_in, {latch});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"stem::ai_controller","needles":["clear_busy"],"why":"grep en el controlador no es un cuerpo"})");
    expect(!wave_apply(&stem_in, ola, ops, &err), "in stem:: rechazado");
    expect(err.find("módulo") != std::string::npos, "msg in no módulo");
    expect(stem_in.campo.empty(), "no reescribe campo");
  }

  {
    WaveOps causal_ops = ops;
    int causal_n = 0;
    causal_ops.peek_causal = [&](const std::string& path, const std::string& symbol,
                                 const std::string& body, bool, std::string* md,
                                 std::vector<WaveHit>* callers, std::string*) {
      ++causal_n;
      if (md == nullptr) {
        return false;
      }
      *md = "----- aguas_arriba " + path + ":" + symbol +
            " -----\ncuando: thinking\nquien:  begin_thinking → set_busy_spinner\n"
            "----- aguas_abajo " +
            path + ":" + symbol + " -----\nbusy_  write\n";
      if (callers != nullptr) {
        WaveHit c;
        c.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
        c.path = "src/ai/ai_controller.cpp";
        c.symbol = "begin_thinking";
        c.stem = "ai_controller";
        c.kind = "fn";
        c.needle = "follow";
        callers->push_back(std::move(c));
      }
      expect(body.empty() || body.find("set_busy_spinner") != std::string::npos ||
                 symbol == "set_busy_spinner" || path.find("busy_strip") != std::string::npos,
             "peek_causal recibe el cuerpo");
      return true;
    };
    WaveState causal;
    causal.prompt = "spinner infinito";
    WaveHit loc = latch;
    loc.files = {"src/ui/busy_strip.cpp", "src/ui/busy_strip.hpp"};
    wave_merge_hits(&causal, {loc});
    std::string err;
    const auto hdr = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"src/ui/busy_strip.hpp","why":"API del latch, no firma causal"})");
    expect(wave_apply(&causal, hdr, causal_ops, &err), "peek header sin causal");
    expect(causal_n == 0, "header no llama peek_causal");
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"cuerpo y firma causal del latch"})");
    expect(wave_apply(&causal, ola, causal_ops, &err), "apply peek causal");
    expect(causal_n >= 1, "función llama peek_causal");
    expect(causal.notas.find("aguas_arriba") == std::string::npos, "aguas no van a notas");
    expect(causal.follow_md.find("aguas_arriba") != std::string::npos, "follow_md aguas");
    expect(causal.follow_md.find("aguas_abajo") != std::string::npos, "follow_md aguas_abajo");
    expect(wave_find_hit(causal.candidatas, "begin_thinking") != nullptr, "caller peekable");
    expect(causal.notas.find("callers:") != std::string::npos, "peek vecinos callers");
    expect(causal.notas.find("begin_thinking") != std::string::npos, "peek caller begin_thinking");
    expect(causal.notas.find("(no leído)") != std::string::npos, "caller no leído marcado");
    expect(causal.notas.find("calls:") != std::string::npos, "peek vecinos calls");
    expect(causal.notas.find("clear_busy_if") != std::string::npos, "peek callee clear_busy_if");
    const std::string cwork = tuide::wave_work_markdown(causal);
    expect(cwork.find("callers:") != std::string::npos, "work callers");
    expect(cwork.find("aguas_arriba") == std::string::npos, "work sigue sin aguas");
    const std::string cnb = tuide::wave_notebook_markdown(causal);
    expect(cnb.find("## Causal") != std::string::npos, "notebook causal");
  }

  {
    WaveOps cross_ops = ops;
    cross_ops.peek_code = [&](const std::string& peek, std::string* text, std::string* err) {
      if (text == nullptr) {
        return false;
      }
      if (peek.find("cancel_current") != std::string::npos) {
        *text =
            "src/ai/ai_controller.cpp:cancel_current\nvoid cancel_current() { cancel_all(); }";
        return true;
      }
      if (err) {
        *err = "peek desconocido";
      }
      return false;
    };
    cross_ops.peek_causal = [&](const std::string&, const std::string& symbol, const std::string&,
                                bool, std::string* md, std::vector<WaveHit>* callers,
                                std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = "----- aguas_arriba -----\ncuando: key\nquien:  handle_ai_console_keys\n";
      if (callers != nullptr && symbol.find("cancel_current") != std::string::npos) {
        WaveHit ui;
        ui.id = "src/ui/console_panel.cpp:handle_ai_console_keys";
        ui.path = "src/ui/console_panel.cpp";
        ui.symbol = "handle_ai_console_keys";
        ui.stem = "console_panel";
        ui.kind = "fn";
        ui.needle = "follow";
        callers->push_back(ui);
        WaveHit route;
        route.id = "src/ai/ai_controller.cpp:handle_route";
        route.path = "src/ai/ai_controller.cpp";
        route.symbol = "handle_route";
        route.stem = "ai_controller";
        route.kind = "fn";
        route.needle = "follow";
        callers->push_back(std::move(route));
      }
      return true;
    };
    cross_ops.rank_hops = [](const std::string&, std::vector<tuide::WavePeekHop>* hops) {
      if (hops == nullptr) {
        return;
      }
      for (auto& h : *hops) {
        if (h.loc.find("console_panel") != std::string::npos ||
            h.loc.find("handle_ai_console_keys") != std::string::npos) {
          h.cosine = 0.81f;
        } else if (h.loc.find("handle_route") != std::string::npos) {
          h.cosine = 0.21f;
        }
      }
    };
    WaveHit latch_ai;
    latch_ai.id = "M8";
    latch_ai.path = "src/ai/ai_controller.cpp";
    latch_ai.symbol = "cancel_current";
    latch_ai.stem = "ai_controller";
    latch_ai.kind = "latch";
    latch_ai.needle = "atlas";
    WaveState cross;
    cross.prompt = "pulsa Escape o clic fuera para cancelar";
    tuide::wave_merge_hits(&cross, {latch_ai});
    tuide::WaveZone zkeep;
    zkeep.id = "M8";
    zkeep.verdict = "keep";
    cross.zonas.push_back(zkeep);
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"cancel_current","why":"cuerpo del abortor y callers"})");
    expect(wave_apply(&cross, ola, cross_ops, &err), "apply peek cross-stem");
    expect(cross.notas.find("handle_ai_console_keys") != std::string::npos,
           "peek muestra caller de otro stem");
    expect(cross.notas.find("otro stem") != std::string::npos, "peek marca otro stem");
    const auto ui_at = cross.notas.find("handle_ai_console_keys");
    const auto route_at = cross.notas.find("handle_route");
    expect(ui_at != std::string::npos && route_at != std::string::npos && ui_at < route_at,
           "rank ficha pone UI antes que handle_route");
    expect(wave_find_hit(cross.candidatas, "handle_ai_console_keys") != nullptr,
           "caller UI peekable aunque keep sea ai_controller");
    const std::string xwork = tuide::wave_work_markdown(cross);
    expect(xwork.find("otro stem") != std::string::npos, "work enseña otro stem");
    expect(xwork.find("aguas_arriba") == std::string::npos, "work sin mermaid aguas");
  }

  {
    WaveOps split_ops = ops;
    split_ops.peek_causal = [&](const std::string&, const std::string&, const std::string&, bool,
                                std::string* md, std::vector<WaveHit>*, std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = std::string(static_cast<std::size_t>(tuide::kWaveFollowChars - 1), 'x') + "\xC3\xB3";
      return true;
    };
    WaveState split;
    split.prompt = "spinner infinito";
    wave_merge_hits(&split, {latch});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"cuerpo del latch no debe partir UTF-8"})");
    expect(wave_apply(&split, ola, split_ops, &err), "apply peek utf8");
    bool dumped = true;
    try {
      (void)tuide::wave_state_to_json(split).dump(2);
    } catch (const std::exception&) {
      dumped = false;
    }
    expect(dumped, "state.json no parte UTF-8");
  }

  {
    WaveOps long_ops = ops;
    long_ops.peek_code = [&](const std::string&, std::string* text, std::string*) {
      if (text == nullptr) {
        return false;
      }
      std::string body = "HEAD_SIG void set_busy_spinner() {\n";
      body += std::string(static_cast<std::size_t>(tuide::kWavePeekChars), 'x');
      body += "\nTAIL_CLEAR clear_busy_if(layout);\n}\n";
      *text = "src/ui/busy_strip.cpp:set_busy_spinner\n" + body;
      return true;
    };
    WaveState ls;
    ls.prompt = "spinner infinito";
    wave_merge_hits(&ls, {latch});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"cuerpo largo: firma y cola"})");
    expect(wave_apply(&ls, ola, long_ops, &err), "apply peek largo");
    expect(ls.notas.find("HEAD_SIG") != std::string::npos, "peek largo conserva firma");
    expect(ls.notas.find("TAIL_CLEAR") != std::string::npos, "peek largo conserva cola");
    expect(ls.notas.find("omitted mid") != std::string::npos, "peek largo marca el medio");
    bool saw_tail_call = false;
    for (const auto& m : ls.mencionados) {
      if (m == "clear_busy_if") {
        saw_tail_call = true;
      }
    }
    expect(saw_tail_call, "call de la cola entra en menciones");
  }

  {
    WaveOps fops = ops;
    fops.follow_tree = [&](const std::string& path, const std::string& symbol, std::string* md,
                           std::vector<WaveHit>* hops, std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = "----- follow " + path + ":" + symbol +
            " causal -----\nS1: paint_frame -> begin_thinking -> set_busy_spinner\n"
            "ON1 when=thinking then=set_busy_spinner\n```mermaid\nflowchart TD\n"
            "  paint_frame[\"paint_frame\"] --> begin_thinking[\"begin_thinking\"]\n```\n";
      if (hops != nullptr) {
        WaveHit a;
        a.path = "src/ai/ai_controller.cpp";
        a.symbol = "begin_thinking";
        a.stem = "ai_controller";
        a.kind = "fn";
        a.needle = "follow";
        WaveHit b;
        b.path = "src/ui/main_layout.cpp";
        b.symbol = "paint_frame";
        b.stem = "main_layout";
        b.kind = "fn";
        b.needle = "follow";
        hops->push_back(std::move(a));
        hops->push_back(std::move(b));
      }
      return true;
    };
    WaveState fs;
    fs.prompt = "spinner infinito";
    wave_merge_hits(&fs, {latch});
    std::string err;
    const auto missing = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"set_busy_spinner","why":"quién llama al latch desde otro barrio"})");
    expect(!wave_apply(&fs, missing, ops, &err), "follow sin causal inyectado");
    expect(err.find("follow") != std::string::npos, "msg sin follow");
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"set_busy_spinner","why":"flujo causal del latch"})");
    expect(ola.do_kind == WaveDo::Follow, "apply do follow");
    fs.propose_n = tuide::kWaveMaxWaves - 1;
    expect(wave_apply(&fs, ola, fops, &err), "follow en penúltima propose");
    expect(fs.follow_md.find(" causal -----") != std::string::npos, "cabecera causal");
    expect(fs.follow_md.find("S1:") != std::string::npos, "stack en follow_md");
    expect(fs.follow_md.find("ON1 when=") != std::string::npos, "rama cond");
    expect(fs.follow_md.find("```mermaid") != std::string::npos, "mermaid en follow_md");
    expect(fs.follow_md.find("begin_thinking") != std::string::npos, "cadena callers");
    expect(wave_find_hit(fs.candidatas, "begin_thinking") != nullptr, "hop ai_controller peekable");
    expect(wave_find_hit(fs.candidatas, "paint_frame") != nullptr, "hop cruza stem peekable");
    expect(fs.notas.find("follow `set_busy_spinner`") != std::string::npos, "nota follow");
    expect(fs.notas.find("callers + callees") != std::string::npos, "nota capacidades");
    const std::string fwork = tuide::wave_work_markdown(fs);
    expect(fwork.find("## Follows") != std::string::npos, "trabajo follows acumulados");
    expect(fwork.find("----- follow ") != std::string::npos, "trabajo cuerpo follow");
    expect(fwork.find("S1:") != std::string::npos, "trabajo stack S1");
    expect(fwork.find("ya seguidos:") != std::string::npos, "trabajo ya seguidos");
    expect(fwork.find("## Hops") != std::string::npos, "trabajo hops");
    expect(fwork.find("begin_thinking") != std::string::npos, "hop en trabajo");
    expect(fwork.find("```mermaid") == std::string::npos, "trabajo sin mermaid");
    expect(fwork.find("## Causal") == std::string::npos, "trabajo sin causal gordo tras follow");
    wave_merge_hits(&fs, {clearer});
    const auto fol2 = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"clear_busy_if","why":"quién llama al apagado del latch"})");
    expect(wave_apply(&fs, fol2, fops, &err), "segundo follow se acumula");
    const auto wboth = tuide::wave_work_markdown(fs);
    expect(wboth.find("set_busy_spinner") != std::string::npos, "primer follow sigue en trabajo");
    expect(wboth.find("clear_busy_if") != std::string::npos, "segundo follow también en trabajo");
    const auto again = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"set_busy_spinner","why":"releer el flujo causal del latch"})");
    expect(!wave_check_barriers(again, fs, &err), "barrera follow repetido");
    expect(err.find("ya hecho") != std::string::npos, "msg ya hecho");
    fs.last_error = err;
    expect(tuide::wave_work_markdown(fs).find("last_error:") != std::string::npos,
           "error follow repetido en cuaderno");
    expect(tuide::wave_work_markdown(fs).find("Siguiente legal") == std::string::npos,
           "sin receta tras follow repetido");
    WaveState zk;
    zk.prompt = fs.prompt;
    WaveHit mkeep = latch;
    mkeep.id = "M1";
    wave_merge_hits(&zk, {mkeep});
    tuide::WaveZone zkeep;
    zkeep.id = "M1";
    zkeep.verdict = "keep";
    zk.zonas.push_back(zkeep);
    expect(wave_apply(&zk, ola, fops, &err), "follow con zona keep");
    expect(wave_find_hit(zk.candidatas, "begin_thinking") != nullptr, "keep no tira hop cruzado");
    expect(wave_find_hit(zk.candidatas, "paint_frame") != nullptr, "keep no tira hop otro stem");
    const auto tanda = wave_parse_ola(
        R"({"action":"ola_v1","do":"tanda","peeks":["M1"],"follows":["set_busy_spinner"],"why":"cuerpo y flujo causal del latch en una ola"})");
    WaveHit m1 = latch;
    m1.id = "M1";
    WaveState t3;
    t3.prompt = fs.prompt;
    wave_merge_hits(&t3, {m1});
    expect(tanda.do_kind == WaveDo::Tanda, "tanda peek+follow");
    expect(wave_apply(&t3, tanda, fops, &err), "apply tanda follow");
    expect(t3.notas.find("set_busy_spinner") != std::string::npos, "tanda peek cuerpo");
    expect(t3.follow_md.find("```mermaid") != std::string::npos, "tanda mermaid");
    expect(wave_find_hit(t3.candidatas, "begin_thinking") != nullptr, "tanda hop peekable");
    expect(tuide::wave_work_markdown(t3).find("----- follow ") != std::string::npos,
           "tanda follow en trabajo");
  }

  {
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    WaveHit endt;
    endt.id = "fn:src/ai/ai_controller.cpp:end_thinking";
    endt.path = "src/ai/ai_controller.cpp";
    endt.symbol = "end_thinking";
    endt.stem = "ai_controller";
    endt.kind = "fn";
    WaveOps eops = ops;
    eops.path_between = [&](const std::string& from, const std::string& to, std::string* md,
                            std::vector<WaveHit>* hops, std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = "----- entre " + from + " → " + to +
            " -----\nbegin_thinking → handle_route → end_thinking\nn=3 hops\n";
      if (hops != nullptr) {
        WaveHit mid;
        mid.path = "src/ai/ai_controller.cpp";
        mid.symbol = "handle_route";
        mid.stem = "ai_controller";
        mid.kind = "fn";
        mid.needle = "entre";
        hops->push_back(std::move(mid));
      }
      return true;
    };
    WaveState es;
    es.prompt = "spinner infinito";
    wave_merge_hits(&es, {begin, endt});
    std::string err;
    const auto missing = wave_parse_ola(
        R"({"action":"ola_v1","do":"entre","from":"begin_thinking","to":"end_thinking","why":"camino del encendido al apagado"})");
    expect(!wave_apply(&es, missing, ops, &err), "entre sin op");
    expect(err.find("entre") != std::string::npos, "msg sin entre");
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"entre","from":"begin_thinking","to":"end_thinking","why":"camino del encendido al apagado"})");
    expect(wave_apply(&es, ola, eops, &err), "apply entre");
    expect(es.follow_md.find("handle_route") != std::string::npos, "cadena entre");
    expect(wave_find_hit(es.candidatas, "handle_route") != nullptr, "hop entre peekable");
    expect(es.notas.find("entre `begin_thinking`") != std::string::npos, "nota entre");
    expect(!wave_apply(&es, ola, eops, &err), "no repetir entre");
    expect(err.find("ya pedido") != std::string::npos, "msg entre ya");
  }

  {
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    WaveOps iops = ops;
    iops.search_in_body = [&](const std::string& path, const std::string& symbol,
                              const std::vector<std::string>& needles, std::string* md,
                              std::vector<int>* hits, std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = "----- in " + path + ":" + symbol + " -----\n";
      for (const auto& n : needles) {
        *md += n + " hits=0\n  (sin match en este cuerpo)\n";
        if (hits != nullptr) {
          hits->push_back(0);
        }
      }
      return true;
    };
    WaveState is;
    is.prompt = "spinner infinito";
    wave_merge_hits(&is, {begin});
    std::string err;
    const auto missing = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"begin_thinking","needles":["catch"],"why":"¿hay catch que trague el error?"})");
    expect(!wave_apply(&is, missing, ops, &err), "in sin grep inyectado");
    expect(err.find("grep") != std::string::npos, "msg sin grep acotado");
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"begin_thinking","needles":["catch","throw"],"why":"¿hay catch que trague el error?"})");
    expect(wave_apply(&is, ola, iops, &err), "apply in grep");
    expect(is.follow_md.find("----- in ") != std::string::npos, "in dump en causal");
    expect(is.notas.find("in `begin_thinking`") != std::string::npos, "nota in");
    expect(is.needles_log.size() >= 2, "log catch y throw");
    expect(is.needles_log[0].in_locus == "begin_thinking", "log in_locus");
    expect(is.needles_log[0].hits == 0, "hits 0 es evidencia");
    const std::string nb = tuide::wave_notebook_markdown(is);
    expect(nb.find("sin match en este cuerpo") != std::string::npos, "notebook in 0");
    expect(!wave_apply(&is, ola, iops, &err), "no repetir in grep");
    expect(err.find("este cuerpo") != std::string::npos, "msg in ya");
    expect(tuide::wave_work_markdown(is).find("last_error:") != std::string::npos,
           "error in repetido en cuaderno");
    expect(tuide::wave_work_markdown(is).find("Siguiente legal") == std::string::npos,
           "sin receta tras in repetido");
    expect(tuide::wave_work_markdown(is).find("## in") != std::string::npos,
           "trabajo in acumulado");
    const auto global = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["catch"],"why":"catch como símbolo del grafo no es el grep"})");
    expect(wave_check_barriers(global, is, &err), "catch global distinto de in");
    WaveHit m1 = begin;
    m1.id = "M1";
    m1.symbol = "set_busy_spinner";
    m1.path = "src/ui/busy_strip.cpp";
    m1.files = {"src/ui/busy_strip.cpp", "src/ai/ai_controller.cpp"};
    WaveState aliased;
    aliased.prompt = is.prompt;
    wave_merge_hits(&aliased, {m1, begin});
    const auto file_in = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"src/ai/ai_controller.cpp","needles":["catch"],"why":"grep en el cpp entero no vale"})");
    expect(file_in.ok, "parse in archivo");
    expect(!wave_check_barriers(file_in, aliased, &err), "in archivo barrera");
    expect(err.find("archivo") != std::string::npos, "msg in archivo");
    const auto path_fn = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"src/ai/ai_controller.cpp:begin_thinking","needles":["catch"],"why":"grep en el caller anclado"})");
    expect(wave_check_barriers(path_fn, aliased, &err), "in path:fn vale");
  }

  {
    const std::string cover = tuide::wave_cover_system_prompt();
    expect(cover.find("primer carácter") != std::string::npos, "cover JSON primero");
    expect(cover.find("juicio") != std::string::npos, "cover juicio");
    expect(cover.find("sistema de la consulta") != std::string::npos, "cover localiza objeto");
    expect(cover.find("Keep vacío no vale") != std::string::npos, "cover keep no vacío");
    expect(cover.find("cubra las preguntas") == std::string::npos, "cover no mapea papeles");
    expect(cover.find("PROHIBIDO drop del caller") == std::string::npos, "cover no receta caller");
    expect(cover.find("latch + caller") == std::string::npos, "cover no receta latch");
    const std::string guion_sys = tuide::wave_guion_system_prompt();
    expect(guion_sys.find("quién arranca el trabajo") == std::string::npos, "guion sin plantilla arranque");
    expect(guion_sys.find("qué lo detiene") == std::string::npos, "guion sin plantilla detiene");
    expect(guion_sys.find("qué queda persistido") == std::string::npos, "guion sin plantilla persistido");
    expect(guion_sys.find("papeles") != std::string::npos, "guion pide papeles");
    expect(guion_sys.find("ESTA consulta") != std::string::npos, "guion de esta consulta");
    expect(guion_sys.find("no el orden de exploración") != std::string::npos, "guion es examen");
    const auto salvaged = tuide::wave_salvage_keep(
        "Task recap...\n{\"action\":\"ola_v1\",\"do\":\"juicio\",\"keep\":[\"M1\",\"M7\"]",
        {"M1", "M2", "M7"});
    expect(salvaged.size() == 2, "salvage keep truncado");
    expect(salvaged[0] == "M1" && salvaged[1] == "M7", "salvage M1 M7");
    const std::string pilot = tuide::wave_pilot_system_prompt();
    expect(pilot.find("tanda") != std::string::npos, "pilot tanda");
    expect(pilot.find("Aguas mismo-stem") == std::string::npos, "pilot sin receta follow");
    expect(pilot.find("bosquejo") != std::string::npos &&
               pilot.find("rayo") != std::string::npos,
           "pilot bosquejo rayos");
    expect(pilot.find("No es el follow (stacks)") != std::string::npos,
           "pilot peek callers vs follow");
    expect(pilot.find("- follow:") != std::string::npos, "pilot follow gesto");
    expect(pilot.find("callers") != std::string::npos && pilot.find("callees") != std::string::npos,
           "pilot follow callers+callees");
    expect(pilot.find("mermaid") != std::string::npos, "pilot follow mermaid");
    expect(pilot.find("recortes") != std::string::npos, "pilot follow recortes");
    expect(pilot.find("\"do\":\"follow\"") != std::string::npos, "pilot JSON follow");
    expect(pilot.find("- entre:") != std::string::npos, "pilot entre gesto");
    expect(pilot.find("No es el mermaid de follow") != std::string::npos, "entre distinto de follow");
    expect(pilot.find("\"do\":\"entre\"") != std::string::npos, "pilot JSON entre");
    expect(pilot.find("files") != std::string::npos, "pilot files/header");
    expect(pilot.find("\"in\":\"src/pkg/mod.cpp:run_job\"") != std::string::npos,
           "pilot JSON in path:fn");
    expect(pilot.find("PROHIBIDO") != std::string::npos &&
               pilot.find(".cpp") != std::string::npos,
           "pilot in no archivo");
    expect(pilot.find("stem::módulo") != std::string::npos, "pilot in no stem campo");
    expect(pilot.find("tú decides cuándo termina") != std::string::npos, "pilot cierra cuando entiende");
    expect(pilot.find("no te retiene") != std::string::npos, "pilot sin vallado de circuito");
    expect(pilot.find("set_busy_spinner") == std::string::npos, "pilot no planta spinner");
    expect(pilot.find("run_level1_async") == std::string::npos, "pilot no planta caller del chat");
    expect(pilot.find("begin_thinking") == std::string::npos, "pilot no planta begin_thinking");
    expect(pilot.find("\"needles\":[\"catch\"") == std::string::npos, "pilot no planta catch");
    expect(pilot.find("no cancela los peeks") != std::string::npos, "pilot tanda peek sobrevive in");
    expect(pilot.find("El Guion es el examen") != std::string::npos, "pilot guion es examen");
    expect(pilot.find("Cover keep localiza el sistema") != std::string::npos, "pilot cover es objeto");
    expect(pilot.find("disparo y efecto") != std::string::npos, "pilot ramas");
    expect(pilot.find("\"do\":\"independiente\"") != std::string::npos, "pilot JSON independiente");
    expect(pilot.find("no redactes un prompt") != std::string::npos, "independiente no redacta");
    expect(pilot.find("me queda esto") != std::string::npos, "independiente induce resto");
    expect(pilot.find("no es \"no busco\"") != std::string::npos, "independiente no salta hueco");
    expect(pilot.find("1 latch + 1 caller") == std::string::npos, "pilot sin receta tanda");
    expect(pilot.find("Tras peek del latch") == std::string::npos, "pilot sin receta cerca");
    expect(pilot.find("No recetes un parche") != std::string::npos, "pilot no parchea no leído");
  }

  {
    expect(tuide::wave_line_has_needle("  abort();", "abort"), "abort palabra");
    expect(!tuide::wave_line_has_needle("  (/cancel para abortar)", "abort"), "abort no abortar");
    expect(!tuide::wave_line_has_needle("retry {", "try"), "try no retry");
    expect(tuide::wave_line_has_needle("  try {", "try"), "try bloque");
    expect(tuide::wave_line_has_needle("std::terminate();", "std::terminate"), "ns palabra");
  }

  {
    WaveState last;
    last.prompt = "spinner infinito";
    last.wave_n = tuide::kWaveMaxWaves;
    const std::string up = tuide::wave_pilot_user_prompt(last);
    expect(up.find("ÚLTIMA OLA") != std::string::npos, "user última ola");
    WaveState last_p;
    last_p.prompt = last.prompt;
    last_p.propose_n = tuide::kWaveMaxWaves - 1;
    last_p.wave_n = 4;
    expect(tuide::wave_pilot_user_prompt(last_p).find("ÚLTIMA OLA") == std::string::npos,
           "penúltima propose no reserva el cierre");
    last_p.propose_n = tuide::kWaveMaxWaves;
    expect(tuide::wave_pilot_user_prompt(last_p).find("ÚLTIMA OLA") != std::string::npos,
           "última ola es propose == max");
    expect(tuide::wave_pilot_user_prompt(last_p).find("Cierra si ya entendiste") == std::string::npos,
           "última ola sin receta de cierre");
    WaveState circuit;
    circuit.prompt = last.prompt;
    circuit.circuit_on = {"src/ai/ai_controller.cpp:begin_thinking"};
    circuit.circuit_off = {"src/ai/ai_controller.cpp:end_thinking"};
    const std::string up2 = tuide::wave_pilot_user_prompt(circuit);
    expect(up2.find("Circuito ON y OFF anclado") == std::string::npos, "sin receta circuito");
    expect(up2.find("Cierra si te basta") == std::string::npos, "circuito no empuja cierre");
    expect(up2.find("## Circuito") != std::string::npos, "user ficha circuito");
    expect(up2.find("## Causal") == std::string::npos, "trabajo sin causal gordo");
    expect(up2.find("## Atlas") == std::string::npos, "trabajo sin atlas");
    expect(static_cast<int>(up2.size()) <= tuide::kWaveWorkChars + 800, "user acotado");
    const std::string work_needles = tuide::wave_work_markdown(run);
    expect(work_needles.find("loci:") != std::string::npos, "needles listan loci");
  }

  {
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    WaveHit endt;
    endt.id = "fn:src/ai/ai_controller.cpp:end_thinking";
    endt.path = "src/ai/ai_controller.cpp";
    endt.symbol = "end_thinking";
    endt.stem = "ai_controller";
    endt.kind = "fn";
    WaveState circ;
    circ.prompt = "spinner infinito";
    wave_merge_hits(&circ, {begin, endt});
    std::string err;
    const auto p_on = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"begin_thinking","why":"ver quién enciende el LED"})");
    expect(wave_apply(&circ, p_on, ops, &err), "peek ON");
    const auto p_off = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"end_thinking","why":"ver quién apaga el LED"})");
    expect(wave_apply(&circ, p_off, ops, &err), "peek OFF");
    expect(tuide::wave_circuit_complete(circ), "circuito ON+OFF");
    const auto catch_ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","in":"begin_thinking","needles":["catch"],"why":"¿el caller traga la excepción?"})");
    expect(wave_check_barriers(catch_ola, circ, &err), "catch legal con circuito");
    const std::string work = tuide::wave_work_markdown(circ);
    expect(work.find("## Circuito") != std::string::npos, "work circuito");
    expect(work.find("## Causal") == std::string::npos, "work sin causal");
    expect(work.find("aguas_arriba") == std::string::npos, "work sin aguas");
    expect(work.find("### peek `begin_thinking`") != std::string::npos, "trabajo retiene peek ON");
    expect(work.find("### peek `end_thinking`") != std::string::npos, "trabajo retiene peek OFF");
    expect(tuide::wave_circuit_cierre(circ).find("end_thinking") != std::string::npos,
           "cierre nombra OFF");
    expect(work.find("bosquejo") != std::string::npos, "circuito bosquejo");
    expect(work.find("(ray)") != std::string::npos, "peek ON/OFF emiten rayos");
    expect(work.find("calls:") != std::string::npos, "peek ON/OFF listan calls");
  }

  {
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    WaveState sk;
    sk.prompt = "spinner infinito";
    tuide::wave_merge_hits(&sk, {begin, latch});
    std::string err;
    const auto p1 = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"begin_thinking","why":"ver quién enciende el LED"})");
    expect(wave_apply(&sk, p1, ops, &err), "sketch peek ON");
    const auto p2 = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"cuerpo del latch del LED"})");
    expect(wave_apply(&sk, p2, ops, &err), "sketch peek latch");
    const auto edges = tuide::wave_sketch_edges(sk);
    bool saw_call = false;
    for (const auto& e : edges) {
      if (e.via == "call" && e.from.find("begin_thinking") != std::string::npos &&
          e.to.find("set_busy_spinner") != std::string::npos) {
        saw_call = true;
      }
    }
    expect(saw_call, "bosquejo begin_thinking → set_busy_spinner");
    const std::string md = tuide::wave_sketch_markdown(sk);
    expect(md.find("begin_thinking") != std::string::npos, "sketch nombra ON");
    expect(md.find("(call)") != std::string::npos, "sketch via call");
    WaveHit endt;
    endt.id = "fn:src/ai/ai_controller.cpp:end_thinking";
    endt.path = "src/ai/ai_controller.cpp";
    endt.symbol = "end_thinking";
    endt.stem = "ai_controller";
    endt.kind = "fn";
    tuide::wave_merge_hits(&sk, {endt});
    const auto p3 = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"end_thinking","why":"ver quién apaga el LED"})");
    expect(wave_apply(&sk, p3, ops, &err), "sketch peek OFF");
    const std::string md2 = tuide::wave_sketch_markdown(sk);
    expect(md2.find("end_thinking") != std::string::npos, "sketch nombra OFF");
    bool off_to_latch = false;
    for (const auto& e : tuide::wave_sketch_edges(sk)) {
      if (e.from.find("end_thinking") != std::string::npos &&
          e.to.find("set_busy_spinner") != std::string::npos) {
        off_to_latch = true;
      }
    }
    expect(!off_to_latch, "end_thinking no apunta al latch");
    expect(md2.find("`M1`") == std::string::npos, "sketch sin M1");
  }

  {
    WaveOps rank_ops = ops;
    rank_ops.peek_causal = [&](const std::string&, const std::string&, const std::string&, bool,
                               std::string* md, std::vector<WaveHit>* callers, std::string*) {
      if (md != nullptr) {
        *md = "----- aguas_arriba -----\n";
      }
      if (callers != nullptr) {
        WaveHit dl;
        dl.path = "src/ai/ai_controller.cpp";
        dl.symbol = "end_download";
        dl.stem = "ai_controller";
        dl.kind = "fn";
        WaveHit bg;
        bg.path = "src/ai/ai_controller.cpp";
        bg.symbol = "begin_thinking";
        bg.stem = "ai_controller";
        bg.kind = "fn";
        callers->push_back(std::move(dl));
        callers->push_back(std::move(bg));
      }
      return true;
    };
    rank_ops.peek_code = [&](const std::string&, std::string* text, std::string*) {
      if (text == nullptr) {
        return false;
      }
      *text =
          "src/ui/busy_strip.cpp:set_busy_spinner\nvoid set_busy_spinner() {\n"
          "  state.label = i18n::tr(busy_activity_i18n_key(activity));\n"
          "  state.last_spinner_ms = steady_now_ms();\n"
          "  paint_ansi_unlocked(&state);\n"
          "  ensure_spinner_thread(&state);\n"
          "}\n";
      return true;
    };
    WaveState rk;
    rk.prompt = "spinner del chat con la IA se queda infinito";
    wave_merge_hits(&rk, {latch});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"set_busy_spinner","why":"cuerpo del latch y callers"})");
    expect(wave_apply(&rk, ola, rank_ops, &err), "peek rank vecinos");
    const auto callers_at = rk.notas.find("callers:");
    expect(callers_at != std::string::npos, "rank callers línea");
    const auto begin_at = rk.notas.find("begin_thinking", callers_at);
    const auto dl_at = rk.notas.find("end_download", callers_at);
    expect(begin_at != std::string::npos && dl_at != std::string::npos && begin_at < dl_at,
           "begin_thinking antes que end_download");
    expect(rk.notas.find("ensure_spinner_thread") != std::string::npos, "call ensure_spinner");
    {
      const auto calls_at = rk.notas.find("calls:");
      expect(calls_at != std::string::npos, "calls línea");
      const auto fan = rk.notas.find("fan-in", calls_at);
      const auto block_end = fan == std::string::npos ? rk.notas.size() : fan;
      const std::string block = rk.notas.substr(calls_at, block_end - calls_at);
      expect(block.find("ensure_spinner_thread") != std::string::npos, "calls ensure");
      expect(block.find("i18n") == std::string::npos, "calls sin i18n");
      expect(block.find("_ms") == std::string::npos, "calls sin _ms");
      expect(block.find("paint_") == std::string::npos, "calls sin paint_");
    }
    WaveOps fops = rank_ops;
    fops.follow_tree = [&](const std::string&, const std::string&, std::string* md,
                           std::vector<WaveHit>*, std::string*) {
      if (md == nullptr) {
        return false;
      }
      *md = "----- follow src/ai/ai_controller.cpp:begin_thinking -----\nS1: begin_thinking\n";
      return true;
    };
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    tuide::wave_merge_hits(&rk, {begin});
    const auto fol = wave_parse_ola(
        R"({"action":"ola_v1","do":"follow","follow":"begin_thinking","why":"flujo del caller que enciende el LED"})");
    expect(wave_apply(&rk, fol, fops, &err), "follow caller ON");
    const auto sk = tuide::wave_sketch_markdown(rk);
    expect(sk.find("begin_thinking") != std::string::npos &&
               sk.find("set_busy_spinner") != std::string::npos,
           "follow cuenta como leído en bosquejo");
    expect(sk.find("(call)") != std::string::npos, "arista caller→latch");
    rk.circuit_callers_off = {"run_level1_async", "handle_route"};
    const auto sk2 = tuide::wave_sketch_markdown(rk);
    expect(sk2.find("hueco:") != std::string::npos && sk2.find("run_level1_async") != std::string::npos,
           "hueco callers OFF no leídos");
  }

  {
    WaveHit restart;
    restart.id = "fn:src/app/application.cpp:restart_lsp_for_workspace";
    restart.path = "src/app/application.cpp";
    restart.symbol = "restart_lsp_for_workspace";
    restart.stem = "application";
    restart.kind = "fn";
    WaveHit opened;
    opened.id = "fn:src/symbols/lsp_symbol_provider.cpp:on_workspace_opened";
    opened.path = "src/symbols/lsp_symbol_provider.cpp";
    opened.symbol = "on_workspace_opened";
    opened.stem = "lsp_symbol_provider";
    opened.kind = "fn";
    WaveHit fail;
    fail.id = "fn:src/symbols/lsp_symbol_provider.cpp:restart_lsp_after_transport_failure";
    fail.path = "src/symbols/lsp_symbol_provider.cpp";
    fail.symbol = "restart_lsp_after_transport_failure";
    fail.stem = "lsp_symbol_provider";
    fail.kind = "fn";
    WaveOps lsp_ops = ops;
    lsp_ops.search_needle = [&](const std::string& needle, const std::string&) {
      if (needle.find("on_workspace_opened") != std::string::npos) {
        return std::vector<WaveHit>{opened};
      }
      if (needle.find("restart_lsp_after_transport_failure") != std::string::npos) {
        return std::vector<WaveHit>{fail};
      }
      return std::vector<WaveHit>{};
    };
    lsp_ops.peek_code = [&](const std::string&, std::string* text, std::string*) {
      if (text == nullptr) {
        return false;
      }
      *text =
          "src/app/application.cpp:restart_lsp_for_workspace\n"
          "void restart_lsp_for_workspace() {\n"
          "  const auto setup = ensure_compile_commands_for_clangd(root, cfg);\n"
          "  symbol_provider_->set_workspace_clangd_options(gcc, index);\n"
          "  symbol_provider_->on_workspace_opened(root, setup.compile_dir);\n"
          "  enqueue_ui_task([this]() { UI_WAKE(&layout_state_, \"app\"); });\n"
          "}\n";
      return true;
    };
    lsp_ops.peek_causal = [&](const std::string&, const std::string& symbol, const std::string&, bool,
                              std::string* md, std::vector<WaveHit>* callers, std::string*) {
      if (md != nullptr) {
        *md = "----- aguas_arriba -----\n";
      }
      if (callers != nullptr && symbol.find("on_workspace_opened") != std::string::npos) {
        WaveHit self;
        self.path = restart.path;
        self.symbol = restart.symbol;
        self.stem = restart.stem;
        self.kind = "fn";
        callers->push_back(std::move(self));
        callers->push_back(fail);
      }
      return true;
    };
    WaveState lsp;
    lsp.prompt = "si el proceso LSP se cae o deja de responder, detectarlo y reiniciarlo";
    wave_merge_hits(&lsp, {restart});
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"restart_lsp_for_workspace","why":"ver el reinicio y a quién llama"})");
    expect(wave_apply(&lsp, ola, lsp_ops, &err), "peek restart lsp");
    {
      const auto calls_at = lsp.notas.find("calls:");
      expect(calls_at != std::string::npos, "lsp calls línea");
      const auto fan = lsp.notas.find("fan-in", calls_at);
      const auto block_end = fan == std::string::npos ? lsp.notas.size() : fan;
      const std::string block = lsp.notas.substr(calls_at, block_end - calls_at);
      const auto opened_at = block.find("on_workspace_opened");
      const auto compile_at = block.find("ensure_compile_commands");
      expect(opened_at != std::string::npos, "calls nombra on_workspace_opened");
      expect(compile_at == std::string::npos || opened_at < compile_at,
             "on_workspace_opened antes que compile_commands");
      expect(block.find("enqueue") == std::string::npos, "calls sin enqueue");
      expect(block.find("UI_WAKE") == std::string::npos && block.find("wake") == std::string::npos,
             "calls sin wake");
    }
    const auto edges = tuide::wave_sketch_edges(lsp);
    bool saw_ray = false;
    bool saw_fan = false;
    for (const auto& e : edges) {
      if (e.via == "ray" && e.from.find("restart_lsp_for_workspace") != std::string::npos &&
          e.to.find("on_workspace_opened") != std::string::npos) {
        saw_ray = true;
      }
      if (e.via == "ray" && e.from.find("restart_lsp_after_transport_failure") != std::string::npos &&
          e.to.find("on_workspace_opened") != std::string::npos) {
        saw_fan = true;
      }
    }
    expect(saw_ray, "bosquejo rayo hacia on_workspace_opened");
    expect(saw_fan, "bosquejo fan-in crash restart");
    const std::string md = tuide::wave_sketch_markdown(lsp);
    expect(md.find("(ray)") != std::string::npos, "markdown marca rayo");
    expect(md.find("rayo = no leído") != std::string::npos, "caption rayos");
  }

  {
    WaveState field;
    field.prompt = "spinner infinito";
    std::string err;
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"needles","needles":["agent_busy_"],"why":"el campo del LED no es un nodo"})");
    expect(wave_apply(&field, ola, ops, &err), "apply field needle");
    expect(field.needles_log.size() == 1 && field.needles_log[0].hits == 0, "field hits 0");
    const std::string nb = tuide::wave_notebook_markdown(field);
    expect(nb.find("campo, no nodo") != std::string::npos, "notebook campo no nodo");
  }

  {
    const auto targets = tuide::wave_cover_peek_targets(
        "M1  kind=latch\n    peek: src/ui/busy_strip.cpp:set_busy_spinner, src/ui/busy_strip.hpp\n"
        "    port: M1=>M7 begin_thinking -calls-> end_thinking\n");
    expect(!targets.empty(), "cover peek no vacío");
    bool saw_on = false;
    bool saw_hpp = false;
    for (const auto& t : targets) {
      if (t.find("set_busy_spinner") != std::string::npos ||
          t.find("end_thinking") != std::string::npos) {
        saw_on = true;
      }
      if (t.find(".hpp") != std::string::npos) {
        saw_hpp = true;
      }
    }
    expect(saw_on, "cover toma peek: de la ficha");
    expect(!saw_hpp, "cover no peek de header suelto");
    expect(static_cast<int>(targets.size()) <= tuide::kWaveCoverPeekMax, "cover peek cap");
  }

  {
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    WaveHit hop;
    hop.id = "fn:src/ai/ai_controller.cpp:run_level1_async";
    hop.path = "src/ai/ai_controller.cpp";
    hop.symbol = "run_level1_async";
    hop.stem = "ai_controller";
    hop.kind = "fn";
    hop.needle = "follow";
    WaveState pack;
    pack.prompt = "spinner infinito";
    tuide::wave_merge_hits(&pack, {begin, hop});
    std::string err;
    const auto p = wave_parse_ola(
        R"({"action":"ola_v1","do":"peek","peek":"begin_thinking","why":"ver quién enciende el LED"})");
    expect(wave_apply(&pack, p, ops, &err), "pack peek ON");
    const auto cl = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerrar","why":"ON en begin_thinking; run_level1_async puede no apagar","huecos":["run_level1_async"]})");
    expect(cl.huecos.size() == 1 && cl.huecos[0] == "run_level1_async", "parse huecos");
    expect(wave_apply(&pack, cl, ops, &err), "pack cerrar");
    const auto h = tuide::wave_pack_handoff(pack);
    bool saw_begin = false;
    bool saw_hueco = false;
    for (const auto& a : h.visto) {
      if (a.find("begin_thinking") != std::string::npos) {
        saw_begin = true;
      }
    }
    for (const auto& a : h.huecos) {
      if (a.find("run_level1_async") != std::string::npos) {
        saw_hueco = true;
      }
    }
    expect(saw_begin, "pack visto begin_thinking");
    expect(saw_hueco, "pack hueco run_level1_async");
    const auto pj = tuide::wave_pack_to_json(pack);
    expect(pj.contains("visto") && pj.contains("huecos"), "pack json keys");
    const std::string md = tuide::wave_pack_markdown(pack);
    expect(md.find("## Pack") != std::string::npos, "pack md");
    expect(md.find("Huecos") != std::string::npos, "pack md huecos");
    expect(pack.cierre.find("Visto:") == 0, "cierre pie Visto");
    expect(pack.cierre.find("Huecos:") != std::string::npos, "cierre pie Huecos");
    expect(pack.cierre.find("no es evidencia") != std::string::npos, "cierre pie no evidencia");
  }

  {
    WaveHit latch;
    latch.id = "M1";
    latch.path = "src/ui/busy_strip.cpp";
    latch.symbol = "set_busy_spinner";
    latch.stem = "busy_strip";
    latch.kind = "latch";
    latch.needle = "atlas";
    WaveHit chrome;
    chrome.id = "M3";
    chrome.path = "src/ui/hover.cpp";
    chrome.symbol = "paint_hover";
    chrome.stem = "hover";
    chrome.kind = "chrome";
    chrome.needle = "atlas";
    WaveHit caller;
    caller.id = "M7";
    caller.path = "src/ai/ai_controller.cpp";
    caller.symbol = "begin_thinking";
    caller.stem = "ai_controller";
    caller.kind = "caller";
    caller.needle = "atlas";
    WaveState cov;
    cov.prompt = "spinner infinito";
    tuide::wave_merge_hits(&cov, {latch, chrome, caller});
    auto drop_caller = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M7","M3"],"why":"el latch posee el LED; el caller no"})");
    expect(drop_caller.ok, "parse cover drop caller");
    std::string err;
    expect(wave_apply(&cov, drop_caller, ops, &err), "apply cover respeta keep/drop");
    bool keep_m1 = false;
    bool keep_m7 = false;
    bool drop_m7 = false;
    for (const auto& z : cov.zonas) {
      if (z.id == "M1" && z.verdict == "keep") {
        keep_m1 = true;
      }
      if (z.id == "M7" && z.verdict == "keep") {
        keep_m7 = true;
      }
      if (z.id == "M7" && z.verdict == "drop") {
        drop_m7 = true;
      }
    }
    expect(keep_m1, "keep latch");
    expect(!keep_m7 && drop_m7, "caller drop se respeta");
  }

  {
    WaveState st;
    st.prompt = "spinner infinito";
    WaveHit begin;
    begin.id = "fn:src/ai/ai_controller.cpp:begin_thinking";
    begin.path = "src/ai/ai_controller.cpp";
    begin.symbol = "begin_thinking";
    begin.stem = "ai_controller";
    begin.kind = "fn";
    tuide::wave_merge_hits(&st, {begin});
    st.peeks_done = {"begin_thinking"};
    WaveOla draft;
    draft.ok = true;
    draft.do_kind = WaveDo::Cerrar;
    draft.why = "ON en begin_thinking; falta ver end_thinking";
    draft.huecos = {"end_thinking"};
    WaveOla peek_ok;
    peek_ok.ok = true;
    peek_ok.do_kind = WaveDo::Peek;
    peek_ok.peeks = {"end_thinking"};
    expect(tuide::wave_close_audit_accept(draft, peek_ok, st), "audit peek hueco");
    WaveOla peek_dup;
    peek_dup.ok = true;
    peek_dup.do_kind = WaveDo::Peek;
    peek_dup.peeks = {"begin_thinking"};
    expect(!tuide::wave_close_audit_accept(draft, peek_dup, st), "audit peek ya leído");
    WaveOla needles;
    needles.ok = true;
    needles.do_kind = WaveDo::Needles;
    needles.needles = {"end_thinking"};
    needles.in_locus = "src/ai/ai_controller.cpp";
    expect(!tuide::wave_close_audit_accept(draft, needles, st), "audit in archivo no");
    WaveOla cerrar;
    cerrar.ok = true;
    cerrar.do_kind = WaveDo::Cerrar;
    cerrar.why = "el why ya cubre el objeto con lo leído";
    expect(tuide::wave_close_audit_accept(draft, cerrar, st), "audit cerrar sí");
    WaveOla indep_no;
    indep_no.ok = true;
    indep_no.do_kind = WaveDo::Independiente;
    indep_no.papel = 1;
    expect(!tuide::wave_close_audit_accept(draft, indep_no, st), "audit independiente sin guion");
    const auto ins0 = tuide::wave_close_audit_instructions(st);
    expect(ins0.find("Si dudas, do=cerrar") != std::string::npos, "audit dudas cierra si no cabe");
    expect(ins0.find("do=independiente") == std::string::npos, "audit sin independiente si no cabe");
    WaveOla alien;
    alien.ok = true;
    alien.do_kind = WaveDo::Peek;
    alien.peeks = {"handle_route"};
    expect(!tuide::wave_close_audit_accept(draft, alien, st), "audit peek no nombrado");
    WaveHit route;
    route.id = "fn:src/ai/ai_controller.cpp:handle_route";
    route.path = "src/ai/ai_controller.cpp";
    route.symbol = "handle_route";
    route.stem = "ai_controller";
    route.kind = "fn";
    tuide::wave_merge_hits(&st, {route});
    st.circuit_off = {"src/ai/ai_controller.cpp:end_thinking"};
    st.circuit_callers_off = {"handle_route", "run_level1_async"};
    WaveOla fol_ok;
    fol_ok.ok = true;
    fol_ok.do_kind = WaveDo::Follow;
    fol_ok.follows = {"handle_route"};
    expect(tuide::wave_close_audit_accept(draft, fol_ok, st), "audit follow caller OFF");
    WaveOla fol_two;
    fol_two.ok = true;
    fol_two.do_kind = WaveDo::Follow;
    fol_two.follows = {"ai_controller::handle_route", "ai_controller::run_level1_async"};
    expect(tuide::wave_close_audit_accept(draft, fol_two, st), "audit follow 2 callers OFF");
    WaveOla fol_peeked;
    fol_peeked.ok = true;
    fol_peeked.do_kind = WaveDo::Follow;
    fol_peeked.follows = {"begin_thinking"};
    expect(tuide::wave_close_audit_accept(draft, fol_peeked, st), "audit follow ya peekeado");
    WaveOla fol_alien;
    fol_alien.ok = true;
    fol_alien.do_kind = WaveDo::Follow;
    fol_alien.follows = {"invented_agent_done"};
    expect(!tuide::wave_close_audit_accept(draft, fol_alien, st), "audit follow no anclado");
    WaveOla fol_mix;
    fol_mix.ok = true;
    fol_mix.do_kind = WaveDo::Follow;
    fol_mix.follows = {"handle_route", "invented_agent_done"};
    expect(!tuide::wave_close_audit_accept(draft, fol_mix, st), "audit follow mixto no");
    st.follows_done = {"handle_route"};
    expect(!tuide::wave_close_audit_accept(draft, fol_ok, st), "audit follow ya hecho");
  }

  {
    WaveHit load;
    load.symbol = "load";
    load.path = "src/ui/busy_strip.cpp";
    load.stem = "busy_strip";
    load.kind = "fn";
    load.needle = "follow";
    WaveHit lock;
    lock.symbol = "lock";
    lock.path = "src/ui/busy_strip.cpp";
    lock.stem = "busy_strip";
    lock.kind = "fn";
    lock.needle = "follow";
    WaveState noise;
    noise.prompt = "spinner infinito";
    tuide::wave_merge_hits(&noise, {load, lock});
    noise.cierre = "el latch hace load/lock/string; falta end_thinking";
    const auto h = tuide::wave_pack_handoff(noise);
    bool saw_load = false;
    bool saw_lock = false;
    bool saw_string = false;
    bool saw_end = false;
    for (const auto& a : h.huecos) {
      saw_load = saw_load || a.find("load") != std::string::npos;
      saw_lock = saw_lock || a.find("lock") != std::string::npos;
      saw_string = saw_string || a.find("string") != std::string::npos;
      saw_end = saw_end || a.find("end_thinking") != std::string::npos;
    }
    expect(!saw_load && !saw_lock && !saw_string, "pack huecos sin ruido load/lock/string");
    expect(saw_end, "pack hueco end_thinking del why");
  }

  {
    WaveState hint;
    hint.prompt = "spinner infinito";
    hint.wave_n = 1;
    hint.opened_ids = {"M1", "M7"};
    const std::string up = tuide::wave_pilot_user_prompt(hint);
    expect(up.find("1 latch + 1 caller") == std::string::npos, "user sin receta tanda");
    expect(up.find("Tras cover") == std::string::npos, "user sin receta post-cover");
    expect(up.find("Si buscas un papel") == std::string::npos, "user sin receta cerca");
    expect(up.find("Un papel sin nombre") == std::string::npos, "user sin receta papel");
    expect(up.find("independiente cabe") == std::string::npos, "user sin cue sin papeles");
    hint.peeks_done = {"M1"};
    const std::string up2 = tuide::wave_pilot_user_prompt(hint);
    expect(up2.find("Si buscas un papel") == std::string::npos, "user post-peek sin receta cerca");
  }

  {
    const std::string body =
        "void handle_route(const AiRouteResult& route) {\n"
        "  switch (route.kind) {\n"
        "    case AiRouteKind::ResolveTool:\n"
        "      run_tool(route.tool_name, route.arg);\n"
        "      break;\n"
        "    case AiRouteKind::EscalateLevel1:\n"
        "      run_level1_async(original);\n"
        "      break;\n"
        "    case AiRouteKind::Help:\n"
        "      append(\"help\");\n"
        "      break;\n"
        "  }\n"
        "}\n";
    const auto calls = tuide::wave_follow_outgoing_calls(body);
    bool saw_tool = false;
    bool saw_l1 = false;
    bool saw_append = false;
    std::string when_tool;
    for (const auto& c : calls) {
      if (c.symbol == "run_tool") {
        saw_tool = true;
        when_tool = c.when;
      }
      if (c.symbol == "run_level1_async") {
        saw_l1 = true;
      }
      if (c.symbol == "append") {
        saw_append = true;
      }
    }
    expect(saw_tool, "outgoing run_tool");
    expect(saw_l1, "outgoing run_level1_async");
    expect(!saw_append, "outgoing omite append");
    expect(when_tool.find("ResolveTool") != std::string::npos, "outgoing cond case");
    const std::string md =
        tuide::wave_follow_outgoing_markdown("src/ai/ai_controller.cpp:handle_route", calls);
    expect(md.find("----- outgoing") != std::string::npos, "outgoing fence");
    expect(md.find("handle_route") != std::string::npos && md.find("run_tool") != std::string::npos,
           "outgoing mermaid handle_route→run_tool");
  }

  {
    WaveHit latch;
    latch.id = "fn:src/pkg/mod.cpp:start_job";
    latch.path = "src/pkg/mod.cpp";
    latch.symbol = "start_job";
    latch.stem = "mod";
    latch.kind = "fn";
    WaveHit abort;
    abort.id = "fn:src/pkg/mod.cpp:cancel_job";
    abort.path = "src/pkg/mod.cpp";
    abort.symbol = "cancel_job";
    abort.stem = "mod";
    abort.kind = "fn";
    WaveOps cops = ops;
    cops.search_cerca = [&](const std::string& query,
                            const std::vector<std::pair<std::string, std::string>>& seed_fns,
                            const std::vector<std::string>&, const std::vector<std::string>&,
                            int hops, std::vector<tuide::WaveCercaHit>* hits, std::string* err,
                            std::string*) {
      if (hits == nullptr) {
        return false;
      }
      expect(!seed_fns.empty(), "cerca semillas de peek/keep");
      expect(hops == 1, "cerca hops default 1");
      expect(query.find("cancel") != std::string::npos, "cerca embebe conceptos");
      tuide::WaveCercaHit row;
      row.id = abort.id;
      row.path = abort.path;
      row.symbol = abort.symbol;
      row.stem = abort.stem;
      row.kind = "fn";
      row.cosine = 0.61f;
      row.hop = 1;
      row.card = "cancel_job writes pending_cancel_";
      hits->push_back(row);
      if (err) {
        err->clear();
      }
      return true;
    };
    WaveState st;
    st.prompt = "quién cancela el trabajo";
    tuide::wave_merge_hits(&st, {latch});
    st.zonas.push_back({"fn:src/pkg/mod.cpp:start_job", "keep"});
    st.peeks_done = {"src/pkg/mod.cpp:start_job"};
    const auto ola = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["cancel work","user abort"],"in":[],"why":"vi el arranque; busco quién cancela"})");
    expect(ola.ok, "apply parse cerca");
    std::string err;
    expect(wave_apply(&st, ola, cops, &err), "apply cerca");
    expect(err.empty(), "apply cerca sin error");
    expect(st.cerca_log.size() == 1, "cerca log");
    expect(st.cerca_log[0].hits == 1, "cerca hits");
    expect(tuide::wave_find_hit(st.candidatas, "cancel_job") != nullptr, "cerca peekable");
    const auto work = tuide::wave_work_markdown(st);
    expect(work.find("## Cerca") != std::string::npos && work.find("cancel_job") != std::string::npos,
           "cuaderno cerca");
    expect(!wave_apply(&st, ola, cops, &err), "cerca duplicada");
    expect(err.find("ya tirada") != std::string::npos, "cerca ya tirada");
    const auto reuse = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["start_job"],"in":[],"why":"no re-embebas el keep leído"})");
    expect(reuse.ok, "parse cerca símbolo leído");
    expect(!wave_apply(&st, reuse, cops, &err), "cerca keep rechazado");
    WaveState empty;
    const auto no_barrio = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerca","needles":["child dead"],"in":[],"why":"sin peek ni keep no hay barrio"})");
    expect(!wave_apply(&empty, no_barrio, cops, &err), "cerca sin barrio");
  }

  {
    WaveState st;
    st.prompt = "si cancelo, el spinner se apaga y el archivo a medias no queda sucio";
    st.atlas_md = "M1  kind=latch\n";
    WaveHit latch;
    latch.id = "M1";
    latch.kind = "latch";
    latch.needle = "atlas";
    tuide::wave_merge_hits(&st, {latch});
    std::string err;
    WaveOps ops;
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"why":"el latch cubre el objeto"})");
    expect(!wave_apply(&st, juicio, ops, &err), "juicio antes de guion");
    expect(err.find("guion") != std::string::npos, "msg primero guion");
    const auto g = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","qué se apaga","qué pasa con el archivo ya escrito"]})");
    expect(wave_apply(&st, g, ops, &err), "apply guion papeles");
    expect(st.papeles.size() == 3, "papeles en estado");
    expect(tuide::wave_needs_cover(st), "cover tras guion con atlas");
    expect(!wave_apply(&st, g, ops, &err), "guion no se repite");
    expect(err.find("ya tirado") != std::string::npos, "msg guion ya tirado");
    const auto miss0 = tuide::wave_guion_uncovered(st);
    expect(miss0.size() == 3, "sin evidencia al nacer");
    const auto work = tuide::wave_work_markdown(st);
    expect(work.find("## Guion") != std::string::npos, "trabajo guion");
    expect(work.find("## Estrategia") != std::string::npos, "trabajo estrategia");
    expect(work.find("quién aborta") != std::string::npos, "trabajo papel");
    expect(work.find("qué tendrías que poder explicar") != std::string::npos, "trabajo contrato guion");
    expect(work.find("no es el orden de exploración") != std::string::npos, "trabajo guion no es plan");
    expect(work.find("[sin evidencia]") != std::string::npos, "trabajo papeles abiertas");
    const auto cover_up = tuide::wave_cover_user_prompt(st);
    expect(cover_up.find("quién aborta") != std::string::npos, "cover user papel");
    expect(cover_up.find("## Estrategia") != std::string::npos, "cover user estrategia");
    expect(cover_up.find("qué tendrías que poder explicar") != std::string::npos, "cover user contrato");
    st.peeks_done = {"cancel_current"};
    st.notas = "### peek `cancel_current`\nvoid cancel_current() { aborta el job; }\n";
    const auto miss1 = tuide::wave_guion_uncovered(st);
    bool still_file = false;
    bool still_abort = false;
    for (const auto& p : miss1) {
      still_file = still_file || p.find("archivo") != std::string::npos;
      still_abort = still_abort || p.find("aborta") != std::string::npos;
    }
    expect(!still_abort, "aborta cubierto por peek");
    expect(still_file, "archivo sigue sin evidencia");
    const auto work1 = tuide::wave_work_markdown(st);
    expect(work1.find("quién aborta") != std::string::npos, "papel cubierto sigue en cuaderno");
    expect(work1.find("[con evidencia]") != std::string::npos, "marca evidencia");
    expect(work1.find("[sin evidencia]") != std::string::npos, "marca abierta");
    const auto pilot_up = tuide::wave_pilot_user_prompt(st);
    expect(pilot_up.find("quién aborta") != std::string::npos, "piloto papel cubierto presente");
    expect(pilot_up.find("archivo ya escrito") != std::string::npos, "piloto papel abierto presente");
    expect(pilot_up.find("qué tendrías que poder explicar") != std::string::npos, "piloto contrato");
    expect(pilot_up.find("## Estrategia") != std::string::npos, "piloto estrategia pin");
    const auto cl = wave_parse_ola(
        R"({"action":"ola_v1","do":"cerrar","why":"el usuario cancela desde handle_user_input"})");
    expect(wave_apply(&st, cl, ops, &err), "cerrar con papel hueco");
    expect(st.cierre.find("Guion:") != std::string::npos, "caption lista guion");
    expect(st.cierre.find("quién aborta") != std::string::npos, "caption papel cubierto");
    expect(st.cierre.find("Preguntas sin evidencia") != std::string::npos, "caption papeles");
    expect(st.cierre.find("archivo") != std::string::npos, "caption archivo");
  }

  {
    WaveState st;
    st.prompt = "si cancelo, el spinner se apaga y el archivo a medias no queda sucio";
    st.atlas_md = "M1  kind=latch\n";
    WaveHit latch;
    latch.id = "M1";
    latch.kind = "latch";
    latch.needle = "atlas";
    tuide::wave_merge_hits(&st, {latch});
    std::string err;
    WaveOps ops;
    const auto g = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","qué se apaga","qué pasa con el archivo ya escrito"]})");
    expect(wave_apply(&st, g, ops, &err), "guion para independiente");
    const auto too_soon = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","papel":3,"why":"ciclo aparte para esa pregunta del guion"})");
    expect(!wave_apply(&st, too_soon, ops, &err), "independiente antes de cover");
    expect(err.find("primero cover") != std::string::npos, "msg cover primero");
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"why":"el latch cubre el objeto"})");
    expect(wave_apply(&st, juicio, ops, &err), "juicio para independiente");
    expect(!wave_apply(&st, too_soon, ops, &err), "independiente antes de peek");
    st.peeks_done = {"cancel_current"};
    st.propose_n = 3;
    expect(tuide::wave_independiente_cue_markdown(st).find("independiente cabe") != std::string::npos,
           "cue tras peek");
    expect(tuide::wave_independiente_cue_markdown(st).find("3") != std::string::npos,
           "cue lista papel abierto");
    const std::string cue_up = tuide::wave_pilot_user_prompt(st);
    expect(cue_up.find("Elige UNA ola.") != std::string::npos, "elige sigue");
    expect(cue_up.find("independiente cabe") != std::string::npos, "user cue junto a elige");
    const auto ins = tuide::wave_close_audit_instructions(st);
    expect(ins.find("do=independiente") != std::string::npos, "audit ofrece independiente");
    expect(ins.find("Si dudas, do=cerrar") == std::string::npos, "audit no empuja cierre si cabe");
    expect(ins.find("Si falta uno y no tiene nombre") == std::string::npos,
           "audit no cierra papel sin locus");
    WaveOla draft_c;
    draft_c.ok = true;
    draft_c.do_kind = WaveDo::Cerrar;
    draft_c.why = "el latch aborta; falta el archivo";
    WaveOla audit_i;
    audit_i.ok = true;
    audit_i.do_kind = WaveDo::Independiente;
    audit_i.papel = 3;
    expect(tuide::wave_close_audit_accept(draft_c, audit_i, st), "audit acepta independiente abierto");
    audit_i.papel = 99;
    expect(!tuide::wave_close_audit_accept(draft_c, audit_i, st), "audit rechaza índice inválido");
    expect(!wave_apply(&st, too_soon, ops, &err), "independiente sin runtime");
    ops.run_independiente = [&](const std::string& prompt, WaveState* child, std::string* cerr) {
      (void)cerr;
      child->prompt = prompt;
      child->peeks_done = {"src/ui/console_panel.cpp:handle_ai_console_keys"};
      child->notas = "void handle_ai_console_keys() { Escape; clic fuera; }\n";
      WaveHit ui;
      ui.path = "src/ui/console_panel.cpp";
      ui.symbol = "handle_ai_console_keys";
      ui.stem = "console_panel";
      ui.needle = "peek";
      tuide::wave_merge_hits(child, {ui});
      return true;
    };
    const auto covered = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","papel":1,"why":"ciclo aparte para esa pregunta del guion"})");
    st.notas = "aborta el job en cancel_current\n";
    expect(!wave_apply(&st, covered, ops, &err), "papel cubierto no sale");
    expect(err.find("evidencia") != std::string::npos, "msg evidencia anclada");
    expect(wave_apply(&st, too_soon, ops, &err), "independiente papel abierto");
    expect(st.notas.find("independiente `3`") != std::string::npos, "pack hijo en notas");
    expect(st.notas.find("handle_ai_console_keys") != std::string::npos, "cuerpo hijo");
    expect(st.independiente_done.size() == 1 && st.independiente_done[0] == 3, "índice hecho");
    expect(!wave_apply(&st, too_soon, ops, &err), "no relanza el mismo papel");
    st.independiente_leaf = true;
    const auto p2 = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","papel":2,"why":"ciclo aparte para esa pregunta del guion"})");
    expect(!wave_apply(&st, p2, ops, &err), "hijo no anida");
    WaveState last = st;
    last.independiente_leaf = false;
    last.independiente_done.clear();
    last.propose_n = tuide::kWaveMaxWaves;
    expect(!tuide::wave_independiente_ok(last, 2, &err), "no en última ola");
    expect(tuide::wave_work_markdown(st).find("1. ") != std::string::npos, "guion numerado");
  }

  {
    WaveState st;
    st.prompt = "si cancelo, el spinner se apaga";
    st.atlas_md = "M1  kind=latch\nM2  kind=chrome\n";
    st.atlas_cards = {{"query", "cancel_generation escape_key"},
                      {"zones", nlohmann::json::array({{{"id", "M1"}}, {{"id", "M2"}}})}};
    WaveHit latch;
    latch.id = "M1";
    latch.kind = "latch";
    latch.needle = "atlas";
    WaveHit chrome;
    chrome.id = "M2";
    chrome.kind = "chrome";
    chrome.needle = "atlas";
    tuide::wave_merge_hits(&st, {latch, chrome});
    st.atlas_seed = st.candidatas;
    std::string err;
    WaveOps ops;
    const auto g = wave_parse_ola(
        R"({"action":"ola_v1","do":"guion","papeles":["quién aborta","qué tecla dispara"]})");
    expect(wave_apply(&st, g, ops, &err), "guion atlas regenerado");
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"why":"el latch cubre el objeto"})");
    expect(wave_apply(&st, juicio, ops, &err), "juicio atlas regenerado");
    st.peeks_done = {"cancel_current"};
    st.propose_n = 3;
    std::string child_query;
    bool child_has_m9 = false;
    bool child_has_m2 = false;
    ops.rebuild_atlas = [&](const std::string& query, WaveState* child, std::string* cerr) {
      (void)cerr;
      child_query = query;
      child->candidatas.clear();
      child->atlas_seed.clear();
      child->opened_ids.clear();
      WaveHit fresh;
      fresh.id = "M9";
      fresh.kind = "caller";
      fresh.stem = "console_panel";
      fresh.symbol = "handle_ai_console_keys";
      fresh.path = "src/ui/console_panel.cpp";
      fresh.needle = "atlas";
      tuide::wave_merge_hits(child, {fresh});
      child->atlas_seed = child->candidatas;
      child->atlas_md = "consulta: " + query + "\nsearch: " + query + "\nM9  kind=caller\n";
      child->atlas_cards = {{"query", query},
                            {"zones", nlohmann::json::array({{{"id", "M9"}}})}};
      return true;
    };
    ops.run_independiente = [&](const std::string&, WaveState* child, std::string* cerr) {
      (void)cerr;
      child_has_m9 = tuide::wave_find_hit(child->candidatas, "M9") != nullptr;
      child_has_m2 = tuide::wave_find_hit(child->candidatas, "M2") != nullptr;
      child->peeks_done = {"src/ui/console_panel.cpp:handle_ai_console_keys"};
      return true;
    };
    const auto ind = wave_parse_ola(
        R"({"action":"ola_v1","do":"independiente","papel":2,"why":"ciclo aparte para esa pregunta del guion"})");
    expect(wave_apply(&st, ind, ops, &err), "independiente regenera atlas");
    expect(child_query == "qué tecla dispara", "rebuild usa el papel");
    expect(child_has_m9, "hijo tiene zonas de su consulta");
    expect(!child_has_m2, "hijo no hereda el mazo del padre");
  }

  {
    WaveState seeded;
    seeded.prompt = "si cancelo, el spinner se apaga";
    seeded.atlas_md = "M8  kind=object\n";
    seeded.control_worker = true;
    expect(!tuide::wave_needs_guion(seeded), "explorador salta guion");
    expect(tuide::wave_needs_cover(seeded), "explorador va a cover");
    std::string err;
    expect(!tuide::wave_independiente_ok(seeded, 1, &err), "explorador no orquesta");
    expect(err.find("orquesta") != std::string::npos, "msg orquesta");
    WaveHit z;
    z.id = "M8";
    z.kind = "object";
    z.needle = "atlas";
    tuide::wave_merge_hits(&seeded, {z});
    WaveOps ops;
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M8"],"why":"esta zona es el sistema de la consulta"})");
    expect(wave_apply(&seeded, juicio, ops, &err), "explorador juicio sin guion");
    const auto exp = tuide::wave_explorer_system_prompt();
    expect(exp.find("NO independiente") != std::string::npos, "explorador veta independiente");
    expect(exp.find("\"do\":\"independiente\"") == std::string::npos, "explorador sin JSON independiente");
    expect(exp.find("no encontré lo que preguntaba") != std::string::npos &&
               exp.find("PROHIBIDO asignar la siguiente caza") != std::string::npos,
           "explorador: cerrar dice si encontró el objeto");
    expect(tuide::wave_pilot_system_prompt().find("independiente") != std::string::npos,
           "piloto sigue teniendo independiente");
    expect(tuide::wave_pilot_user_prompt(seeded).find("independiente cabe") == std::string::npos,
           "explorador sin cue");
    expect(tuide::wave_cover_user_prompt(seeded).find("aún no hay preguntas") == std::string::npos,
           "cover explorador sin guion vacío");
    expect(tuide::wave_control_consulta_ok("dónde se leen las pulsaciones de teclado en la interfaz",
                                          &err),
           "consulta gesto estrecho");
    expect(tuide::wave_control_consulta_ok(
               "dónde se captura la tecla o el clic que cancela la generación", &err),
           "NL con o es legal; el piloto atomiza por política");
    expect(tuide::wave_control_consulta_ok(
               "dónde se interrumpe la escritura del archivo al cancelar y cómo se vinculan Escape o clic fuera",
               &err),
           "NL gordo es legal; el piloto atomiza por política");
    const auto mix = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde se maneja el clic fuera y cómo se limpia el archivo a medias","why":"quedan dos objetos"})");
    expect(mix.ok && mix.do_kind == tuide::WaveControlDo::Explorar, "parse no caza o ni mezcla");
    expect(tuide::wave_control_rama_nudge(mix.consulta).find("Atomiza") != std::string::npos &&
               tuide::wave_control_rama_nudge(mix.consulta).find("Rechazado") != std::string::npos,
           "nudge pide atomizar; cita lo rechazado");
    expect(tuide::wave_control_rama_nudge(mix.consulta).find("un locator cada") == std::string::npos,
           "nudge sin receta locator+locator");
    const std::string ancla =
        "cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias";
    expect(!tuide::wave_control_consulta_delta_ok(ancla, ancla, {}, &err), "no recap ancla");
    expect(tuide::wave_control_consulta_delta_ok(
               "dónde se leen las pulsaciones de teclado en la interfaz", ancla, {}, &err),
           "desplazamiento gesto");
    const std::string t1_esc =
        "dónde se captura el evento de escape en el controlador de IA";
    const std::string t2_esc =
        "dónde se registra el listener de la tecla Escape en el controlador de IA";
    expect(tuide::wave_control_consulta_delta_ok(t2_esc, ancla, {t1_esc}, &err),
           "paráfrasis pasa el recap de palabras");
    expect(tuide::wave_control_consulta_misma_caza(t2_esc, {t1_esc}) == t1_esc,
           "mismo objeto: escape+controlador");
    expect(tuide::wave_control_consulta_misma_caza(
               "dónde se leen las pulsaciones de teclado en la interfaz", {t1_esc})
               .empty(),
           "teclado no es la caza de escape");
    expect(tuide::wave_control_consulta_misma_caza(
               "dónde se aborta el trabajo en curso", {t1_esc})
               .empty(),
           "parada no es la caza de escape");
    const auto misma_nudge = tuide::wave_control_misma_caza_nudge(t1_esc, t2_esc);
    expect(misma_nudge.find(t1_esc) != std::string::npos &&
               misma_nudge.find("otro verbo") != std::string::npos &&
               misma_nudge.find("Rechazado") != std::string::npos,
           "nudge cita la caza ya tirada");
    expect(!tuide::wave_control_consulta_ok("src/ai/ai_controller.cpp", &err), "consulta path");
    expect(!tuide::wave_control_consulta_ok("handle_escape_key", &err), "consulta stem");
    expect(!tuide::wave_control_consulta_ok("qué hace M8", &err), "consulta M*");
    expect(tuide::wave_control_consulta_ok(
               "dónde se aborta la escritura al cancelar la generación", &err),
           "consulta NL");
    expect(!tuide::wave_control_consulta_ok(
               "dónde se modifican los archivos durante la generación", &err),
           "categoría archivos no es un objeto");
    expect(err.find("consulta sin objeto") != std::string::npos, "msg sin objeto");
    expect(tuide::wave_control_consulta_ok(
               "dónde se revierten los archivos al cancelar la generación", &err),
           "revertir archivos nombra el mecanismo");
    expect(tuide::wave_control_consulta_ok(
               "dónde se restauran los archivos al abortar la generación", &err),
           "restaurar archivos nombra el mecanismo");
    expect(tuide::wave_control_consulta_ok(
               "dónde se invoca la lógica de reversión de archivos tras abortar", &err),
           "reversión de archivos es objeto, no categoría");
    expect(tuide::wave_control_consulta_ok(
               "dónde se deshacen los cambios escritos al abortar la generación", &err),
           "deshacer nombra el mecanismo");
    expect(tuide::wave_control_consulta_ok(
               "el trabajo anterior cerró que la parada no vive en el controlador; sigue cómo entra "
               "la señal de entrada del usuario y si se traduce a esa parada. no cubras otro disparo.",
               &err),
           "consulta briefing largo");
    expect(tuide::wave_control_consulta_ok("qué tecla aborta el trabajo en curso.", &err),
           "consulta con punto");
    const auto bad = tuide::wave_parse_control("no json");
    expect(!bad.ok, "control sin JSON");
    const auto explora = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde se leen las pulsaciones de teclado en la interfaz","why":"primer eslabón: el gesto, todavía no la ia"})");
    expect(explora.ok, "parse explorar");
    expect(explora.do_kind == tuide::WaveControlDo::Explorar, "do explorar");
    expect(explora.consultas.size() == 1, "consulta suelta = un explorador");
    const auto explora_h = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde captura la consola el evento de entrada del usuario","hacia":["entrada usuario"],"why":"el controlador ya se cerró; esta rama es la señal"})");
    expect(explora_h.ok && explora_h.hacia.size() == 1 && explora_h.hacia[0] == "entrada usuario",
           "explorar admite hacia de cerca");
    const auto explora_hacia2 = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde captura la consola el evento de entrada del usuario","hacia":["entrada usuario","generacion"],"why":"dos objetos en hacia"})");
    expect(!explora_hacia2.ok && explora_hacia2.error.find("hacia") != std::string::npos,
           "hacia no empaqueta dos zonas");
    const auto explora_stem = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde captura la consola el evento de entrada del usuario","hacia":["src/ui"],"why":"quiero clavar el directorio"})");
    expect(!explora_stem.ok, "hacia no es path");
    const auto dos = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consultas":["dónde se leen las pulsaciones de teclado en la interfaz","dónde se para la generación en curso del agente"],"why":"dos pasos a la vez"})");
    expect(!dos.ok, "un explorador por turno");
    const auto tres = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consultas":["dónde se leen las pulsaciones de teclado en la interfaz","dónde se para la generación en curso del agente","qué hilo aborta el trabajo en curso ahora"],"why":"tres zonas a la vez"})");
    expect(!tres.ok, "máx uno por turno");
    const auto plan_old = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","pasos":["dónde se registra el evento de entrada del usuario","dónde se detiene el trabajo que está en curso","si ese evento llega a detener el trabajo"],"why":"el ancla junta entrada y parada"})");
    expect(!plan_old.ok, "plan con pasos se rechaza");
    const auto plan = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se registra el evento de entrada del usuario"},{"id":"B","kind":"locator","hacia":["parada trabajo"]},{"id":"P","kind":"puente","need":["A","B"]}],"why":"el mapa no une disparo y parada"})");
    expect(plan.ok && plan.do_kind == tuide::WaveControlDo::Plan && plan.plan.fases.size() == 3,
           "parse plan romper");
    expect(plan.plan.modo == tuide::WaveControlPlanModo::Romper, "modo romper");
    expect(plan.plan.fases[2].kind == tuide::WaveControlPhaseKind::Puente, "fase puente");
    expect(plan.plan.fases[1].consulta.empty() && !plan.plan.fases[1].hacia.empty(),
           "locator posterior: hacia, no consulta");
    const auto plan_seguir = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"seguir","fases":[{"id":"B","kind":"locator","consulta":"dónde se detiene el trabajo que está en curso"},{"id":"S","kind":"seguir","need":["B"],"hacia":["evento","clic"]}],"why":"un ancla y tirar del flujo"})");
    expect(plan_seguir.ok && plan_seguir.plan.fases.size() == 2, "parse plan seguir");
    const auto plan_corto = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se registra el evento de entrada del usuario"}],"why":"un solo paso no es plan"})");
    expect(!plan_corto.ok, "plan mínimo 2 fases");
    const auto plan_sin_loc = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"P","kind":"puente","need":["A"]},{"id":"Q","kind":"puente","need":["P"]}],"why":"puente sin locator no vale"})");
    expect(!plan_sin_loc.ok, "plan sin locator");
    const auto plan_cat = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se modifican los archivos durante la generación"},{"id":"B","kind":"locator","hacia":["parada trabajo"]},{"id":"P","kind":"puente","need":["A","B"]}],"why":"el mapa no une disparo y efecto"})");
    expect(!plan_cat.ok && plan_cat.error.find("consulta sin objeto") != std::string::npos,
           "plan no admite locator de categoría");
    const auto plan_post = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se registra el evento de entrada del usuario"},{"id":"B","kind":"locator","consulta":"dónde se detiene el trabajo que está en curso"},{"id":"P","kind":"puente","need":["A","B"]}],"why":"congelé B"})");
    expect(!plan_post.ok && plan_post.error.find("locator posterior") != std::string::npos,
           "locator posterior no congela consulta");
    const auto packed_o = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"dónde se intercepta la tecla escape o el foco perdido","why":"dos disparos en una caza"})");
    expect(packed_o.ok, "explorar con o es NL; no lo caza el runtime");
    const auto plan_o = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se captura el evento de escape o la pérdida de foco"},{"id":"B","kind":"locator","hacia":["parada trabajo"]},{"id":"P","kind":"puente","need":["A","B"]}],"why":"dos mecanismos distintos y el hijo de A no necesita B"})");
    expect(plan_o.ok && plan_o.plan.fases.size() == 3, "plan no caza o en el locator");
    const auto pasar = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"pasar","why":"esta fase ya tiene un locus leído"})");
    expect(pasar.ok && pasar.do_kind == tuide::WaveControlDo::Pasar, "parse pasar");
    const auto nopasar = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"no_pasar","why":"esta fase no encontró el objeto"})");
    expect(nopasar.ok && nopasar.do_kind == tuide::WaveControlDo::NoPasar, "parse no_pasar");
    const auto revisar = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"revisar","consulta":"dónde se evalúa el evento de entrada del usuario","why":"el locator era demasiado ancho"})");
    expect(revisar.ok && revisar.do_kind == tuide::WaveControlDo::Revisar, "parse revisar");
    const auto bosqueja = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"bosquejar","hacia":["evento entrada","parada trabajo","archivo"],"why":"quiero ver si el ancla vive en un barrio o en varios"})");
    expect(bosqueja.ok && bosqueja.do_kind == tuide::WaveControlDo::Bosquejar &&
               bosqueja.hacia.size() == 3,
           "parse bosquejar");
    const auto bosqueja_alias = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"bosquejar","conceptos":["evento entrada","clic","parada","archivo"],"why":"olores de zona, no nombres del ancla"})");
    expect(bosqueja_alias.ok && bosqueja_alias.hacia.size() == 4, "bosquejar admite conceptos");
    const auto bosqueja_corto = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"bosquejar","hacia":["solo uno"],"why":"un concepto no bosqueja"})");
    expect(!bosqueja_corto.ok, "bosquejar mínimo 2");
    const auto bosqueja_peek = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"bosquejar","hacia":["handle ai console keys","cancelación"],"why":"copié un peek de cuatro palabras"})");
    expect(!bosqueja_peek.ok, "bosquejar no admite peek de 4 palabras");
    const auto zoom = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"zoom","ids":["ui","keys"],"why":"quiero ver el barrio de entrada"})");
    expect(zoom.ok && zoom.do_kind == tuide::WaveControlDo::Zoom && zoom.ids.size() == 2,
           "parse zoom barrio+stem");
    const auto zoom_m = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"zoom","ids":["M1"],"why":"quiero abrir esa ficha"})");
    expect(!zoom_m.ok, "zoom no admite M*");
    const auto zoom_path = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"zoom","ids":["src/ui/keys"],"why":"quiero abrir ese path"})");
    expect(!zoom_path.ok, "zoom no admite path");
    const auto zoom_dot = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"zoom","ids":["keys.cpp"],"why":"quiero abrir ese archivo"})");
    expect(!zoom_dot.ok, "zoom no admite extensión");
    const auto agujas = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["event","mouse","ai","file"],"why":"el ancla toca entrada, clic, generación y archivo"})");
    expect(agujas.ok && agujas.do_kind == tuide::WaveControlDo::Agujas && agujas.hacia.size() == 4,
           "parse agujas");
    expect(agujas.hacia[0] == "event" && agujas.hacia[2] == "ai", "agujas conservan zona");
    const auto agujas_alias = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","needles":["key","mouse"],"why":"luz corta para calibrar el plano"})");
    expect(agujas_alias.ok && agujas_alias.hacia.size() == 2, "agujas admite needles");
    const auto agujas_frase = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["evento entrada","generacion"],"why":"clases de sitio de dos palabras"})");
    expect(agujas_frase.ok && agujas_frase.hacia.size() == 2, "agujas admite clase de sitio");
    const auto agujas_ident = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["Escape","cancel"],"why":"idents del ancla no son zonas"})");
    expect(!agujas_ident.ok, "agujas no admite ident PascalCase");
    const auto agujas_snake = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["cancel_job","mouse"],"why":"un símbolo no es una zona"})");
    expect(!agujas_snake.ok, "agujas no admite snake_case");
    const auto agujas_path = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["src/ui","event"],"why":"un path no calibra el censo"})");
    expect(!agujas_path.ok, "agujas no admite path");
    const auto agujas_m = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["M1","event"],"why":"un M no calibra el censo"})");
    expect(!agujas_m.ok, "agujas no admite M*");
    const auto agujas_una = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"agujas","agujas":["event"],"why":"una sola zona no calibra"})");
    expect(!agujas_una.ok, "agujas mínimo 2");
    std::vector<std::string> luz = {"event", "mouse"};
    expect(tuide::wave_control_agujas_merge(&luz, {"event", "ai", "file"}) == 2, "agujas acumulan");
    expect(luz.size() == 4 && luz.back() == "file", "luz acumulada");
    expect(tuide::wave_control_agujas_merge(&luz, {"Event", "MOUSE"}) == 0, "no repite zona");
    tuide::WaveControlBosquejo foto;
    tuide::WaveControlOlor a;
    a.concepto = "escape";
    a.barrio = "ui";
    a.concentration = 0.72f;
    a.hits = 4;
    tuide::WaveControlOlor b;
    b.concepto = "cancelación";
    b.barrio = "ai";
    b.concentration = 0.61f;
    b.twin = true;
    b.twin_barrio = "ui";
    b.hits = 5;
    foto.olores = {a, b};
    foto.entre.push_back({"ui", "ai"});
    foto.nota = "el mapa no une; un explorar cubre un barrio";
    const std::string foto_md = tuide::wave_control_bosquejo_markdown(foto);
    expect(foto_md.find("escape ~ ui") != std::string::npos &&
               foto_md.find("cancelación ~ ai") != std::string::npos &&
               foto_md.find("ui→ai") != std::string::npos &&
               foto_md.find("no une") != std::string::npos,
           "foto colapsa a barrios");
    expect(foto_md.find("handle") == std::string::npos &&
               foto_md.find("src/") == std::string::npos,
           "foto sin peek ni path");
    expect(tuide::wave_control_barrio_of_path("src/ui/console_panel.cpp:handle_ai_console_keys") ==
               "ui",
           "barrio ui");
    expect(tuide::wave_control_barrio_of_path("src/ai/ai_controller.cpp") == "ai", "barrio ai");
    tuide::WaveControlPlan board;
    std::string perr;
    expect(tuide::wave_control_plan_commit(&board, plan, &perr), "commit plan");
    expect(tuide::wave_control_plan_current(board) == 0, "primer locator en curso");
    const auto t0 = tuide::wave_control_legal({}, 0, false);
    expect(tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Plan) &&
               tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Ampliar) &&
               !tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Bosquejar) &&
               !tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Agujas) &&
               !tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Zoom) &&
               !tuide::wave_control_do_allowed(t0, tuide::WaveControlDo::Pasar),
           "turno 0: fichas (ampliar), explorar o plan");
    expect(!tuide::kWaveControlCatalogLive && !tuide::kWaveControlBosquejoLive,
           "censo y bosquejo desconectados del piloto");
    const auto t0agotado = tuide::wave_control_legal({}, 0, false, 1);
    expect(tuide::wave_control_do_allowed(t0agotado, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t0agotado, tuide::WaveControlDo::Ampliar) &&
               !tuide::wave_control_do_allowed(t0agotado, tuide::WaveControlDo::Agujas) &&
               !tuide::wave_control_do_allowed(t0agotado, tuide::WaveControlDo::Zoom),
           "censo off: agujas_n no cambia lo legal");
    const auto t0zoomed = tuide::wave_control_legal({}, 0, false, 1, 1);
    expect(tuide::wave_control_do_allowed(t0zoomed, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t0zoomed, tuide::WaveControlDo::Plan) &&
               tuide::wave_control_do_allowed(t0zoomed, tuide::WaveControlDo::Ampliar) &&
               !tuide::wave_control_do_allowed(t0zoomed, tuide::WaveControlDo::Agujas),
           "censo off: zoom_n no cambia lo legal");
    const auto t0amp2 = tuide::wave_control_legal({}, 0, false, 0, 0, 2);
    expect(!tuide::wave_control_do_allowed(t0amp2, tuide::WaveControlDo::Ampliar) &&
               tuide::wave_control_do_allowed(t0amp2, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t0amp2, tuide::WaveControlDo::Plan),
           "tras 2 tandas de ampliar: explorar o plan, no más inspect");
    const auto t0amp1 = tuide::wave_control_legal({}, 0, false, 0, 0, 1);
    expect(tuide::wave_control_do_allowed(t0amp1, tuide::WaveControlDo::Ampliar),
           "una tanda de ampliar no cierra inspect");
    const auto t1free = tuide::wave_control_legal({}, 1, true);
    expect(tuide::wave_control_do_allowed(t1free, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t1free, tuide::WaveControlDo::Plan) &&
               !tuide::wave_control_do_allowed(t1free, tuide::WaveControlDo::Zoom) &&
               tuide::wave_control_do_allowed(t1free, tuide::WaveControlDo::Cerrar),
           "tras un job: aún puede partir");
    const auto t0b = tuide::wave_control_legal(board, 1, true);
    expect(!tuide::wave_control_do_allowed(t0b, tuide::WaveControlDo::Explorar) &&
               tuide::wave_control_do_allowed(t0b, tuide::WaveControlDo::Pasar),
           "con plan no se explora libre");
    expect(!tuide::wave_control_plan_pasar(&board, {}, &perr), "pasar sin visto");
    expect(tuide::wave_control_plan_pasar(&board, {"src/pkg/mod.cpp:read_event"}, &perr),
           "pasar con visto");
    expect(board.fases[0].status == tuide::WaveControlPhaseStatus::Paso, "A pasó");
    expect(board.fases[1].status == tuide::WaveControlPhaseStatus::EnCurso, "B en curso");
    expect(tuide::wave_control_plan_pasar(&board, {"src/pkg/mod.cpp:stop_job"}, &perr),
           "pasar B");
    expect(board.fases[2].status == tuide::WaveControlPhaseStatus::EnCurso, "puente en curso");
    const auto spec = tuide::wave_control_launch_spec(board);
    expect(spec.ok && spec.kind == tuide::WaveControlPhaseKind::Puente, "launch puente");
    expect(spec.pin_from.find("read_event") != std::string::npos &&
               spec.pin_to.find("stop_job") != std::string::npos,
           "pin from/to del pack");
    tuide::WaveControlPlan failed;
    expect(tuide::wave_control_plan_commit(&failed, plan, &perr), "commit 2");
    expect(tuide::wave_control_plan_no_pasar(&failed, &perr), "no_pasar A");
    expect(failed.fases[0].status == tuide::WaveControlPhaseStatus::Fallo, "A falló");
    expect(failed.fases[1].status == tuide::WaveControlPhaseStatus::EnCurso, "no_pasar salta a B");
    const auto tfail = tuide::wave_control_legal(failed, 1, false);
    expect(tuide::wave_control_do_allowed(tfail, tuide::WaveControlDo::Revisar) &&
               !tuide::wave_control_do_allowed(tfail, tuide::WaveControlDo::Pasar),
           "tras salto: briefing de B");
    expect(tuide::wave_control_plan_revisar(
               &failed, "dónde se evalúa el evento de entrada del usuario", {}, &perr),
           "revisar locator");
    expect(failed.fases[0].status == tuide::WaveControlPhaseStatus::Fallo, "A sigue fallida");
    expect(failed.fases[1].status == tuide::WaveControlPhaseStatus::EnCurso, "revisar rellena B");
    expect(failed.fases[1].consulta.find("evento de entrada") != std::string::npos,
           "consulta de B al lanzar");
    expect(tuide::wave_control_plan_no_pasar(&failed, &perr), "no_pasar B");
    expect(failed.fases[1].status == tuide::WaveControlPhaseStatus::Fallo, "B falló");
    expect(tuide::wave_control_plan_current(failed) < 0, "puente no arranca sin A/B en paso");
    const auto tskip = tuide::wave_control_legal(failed, 1, false);
    expect(tuide::wave_control_do_allowed(tskip, tuide::WaveControlDo::Cerrar) &&
               !tuide::wave_control_do_allowed(tskip, tuide::WaveControlDo::Revisar),
           "sin fase en curso: cerrar");
    tuide::WaveControlPlan seguir_board;
    expect(tuide::wave_control_plan_commit(&seguir_board, plan_seguir, &perr), "commit seguir");
    expect(tuide::wave_control_plan_pasar(&seguir_board, {"src/pkg/mod.cpp:stop_job"}, &perr),
           "pasar semilla");
    const auto spec_s = tuide::wave_control_launch_spec(seguir_board);
    expect(spec_s.ok && spec_s.kind == tuide::WaveControlPhaseKind::Seguir, "launch seguir");
    expect(spec_s.cerca_hops_max == tuide::kWaveControlSeguirHops, "seguir hops altos");
    expect(!spec_s.hacia.empty(), "seguir hacia");
    WaveState pinned;
    tuide::wave_control_seed_launch(&pinned, spec_s);
    expect(pinned.pin_from.find("stop_job") != std::string::npos, "seed pin_from");
    expect(pinned.cerca_hops_max == tuide::kWaveControlSeguirHops, "seed hops");
    tuide::WaveControlLaunch inherited;
    tuide::wave_control_inherit_visto(
        &inherited, {"src/pkg/mod.cpp:read_event", "src/pkg/mod.cpp:stop_job", "src/pkg/mod.cpp:read_event"});
    expect(inherited.pin_loci.size() == 2, "inherit visto único");
    WaveState handed;
    handed.prompt = "dónde se detiene el trabajo que está en curso";
    handed.control_worker = true;
    tuide::wave_control_seed_launch(&handed, inherited);
    const auto cover_hand = tuide::wave_cover_user_prompt(handed);
    expect(cover_hand.find("Ya leídos en un trabajo anterior") != std::string::npos,
           "hijo ve inventario, no tesis");
    expect(cover_hand.find("SOLO la consulta") != std::string::npos, "hijo no recap");
    expect(cover_hand.find("NO cancelan") == std::string::npos, "sin conclusiones del padre");
    expect(tuide::wave_control_consulta_es_ancla(ancla, ancla), "ancla es ancla");
    expect(!tuide::wave_control_consulta_es_ancla(
               "dónde se leen las pulsaciones de teclado en la interfaz", ancla),
           "rebanada no es ancla");
    const auto copia = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"peek de handle_escape_key","why":"el hueco parece un id y lo copio"})");
    expect(!copia.ok, "no copiar id");
    const auto cierra = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"cerrar","why":"visto el job y el disparo; el archivo sigue hueco"})");
    const auto amplia = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"ampliar","ids":["M4","M7"],"why":"ui: picker vs panel; no sé cuál captura el clic"})");
    expect(amplia.ok, "parse ampliar");
    expect(amplia.do_kind == tuide::WaveControlDo::Ampliar, "do ampliar");
    expect(amplia.ids.size() == 2, "dos fichas");
    const auto amplia_mala = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"ampliar","ids":["M4","M7","M8","M1"],"why":"quiero ver todas las zonas de golpe"})");
    expect(!amplia_mala.ok, "ampliar máx 3");
    WaveState packed;
    packed.prompt = "si cancelo, el spinner se apaga";
    packed.opened_ids = {"M8"};
    packed.cierre =
        "Visto: `src/ai/ai_controller.cpp:cancel_current`\nHuecos: `agent_cancel_ check`\n"
        "(El why no es evidencia de lo no leído.)\n"
        "el job aborta aquí:\n```cpp\nvoid cancel() { join(); }\n```\nfalta el gesto de UI";
    packed.peeks_done = {"src/ai/ai_controller.cpp:cancel_current"};
    packed.huecos_claimed = {"agent_cancel_ check", "el gesto de UI", "abort del job",
                             "blur event handler"};
    const auto brief = tuide::wave_control_brief(packed);
    expect(brief.find("```") == std::string::npos, "brief sin fences");
    expect(brief.find("M8") != std::string::npos, "brief keep");
    expect(brief.find("Cerrado:") != std::string::npos, "brief cerrado");
    expect(brief.find("el job aborta") != std::string::npos, "brief tesis");
    expect(brief.find("cancel_current") == std::string::npos, "brief sin dump de paths");
    expect(brief.find("src/") == std::string::npos, "brief sin src");
    expect(brief.find("agent_cancel_") == std::string::npos, "brief sin id hueco");
    expect(brief.find("gesto de UI") != std::string::npos, "brief rol abierto");
    expect(brief.find("abort del job") != std::string::npos, "abierto no recorta el tercer hueco");
    expect(brief.find("blur event handler") != std::string::npos, "abierto guarda blur");
    WaveState packed2 = packed;
    packed2.huecos_claimed = {"agent_cancel_", "tasks_.cancel()"};
    packed2.cierre =
        "(El why no es evidencia de lo no leído.)\n"
        "Escape/clic fuera NO cancelan actualmente a menos que el UI los traduzca. "
        "La limpieza de archivo a medias no existe.";
    const auto brief2 = tuide::wave_control_brief(packed2);
    expect(brief2.find("agent_cancel_") == std::string::npos, "tesis sin id hueco");
    expect(brief2.find("el gesto de tecla o clic que cancela la generación") == std::string::npos,
           "abierto no se inventa desde el cierre");
    expect(brief2.find("cómo queda el archivo") == std::string::npos, "sin receta de archivo");
    WaveState packed_deny = packed;
    packed_deny.prompt = "dónde se captura el evento de escape en el controlador de IA";
    packed_deny.cierre =
        "(El why no es evidencia de lo no leído.)\n"
        "ai_trace_escape es sanitización de strings, NO la captura del evento de teclado. "
        "El nombre es engañoso.";
    const auto brief_deny = tuide::wave_control_brief(packed_deny);
    expect(brief_deny.find("No se encontró el objeto de esta caza") != std::string::npos &&
               brief_deny.find("No la relances") != std::string::npos &&
               brief_deny.find("Preguntó:") != std::string::npos,
           "brief: caza fallida si lo leído no es el objeto");
    expect(brief_deny.find("Abierto:") == std::string::npos &&
               brief_deny.find("no el siguiente encargo") != std::string::npos,
           "caza fallida: huecos no son el siguiente briefing");
    WaveState packed_rima = packed_deny;
    packed_rima.cierre =
        "(El why no es evidencia de lo no leído.)\n"
        "ai_trace_escape es una utilidad de sanitización de strings, no un manejador de eventos "
        "de teclado. Falta localizar el handler de keypress.";
    const auto brief_rima = tuide::wave_control_brief(packed_rima);
    expect(brief_rima.find("No se encontró el objeto de esta caza") != std::string::npos,
           "brief: no un manejador = caza fallida");
    expect(brief_rima.find("Abierto:") == std::string::npos, "fallo: Abierto no es menú");
    WaveState packed3 = packed;
    packed3.huecos_claimed = {"src/ai/ai_controller.cpp:cancel_level1", "cancel"};
    packed3.cierre =
        "(El why no es evidencia de lo no leído.)\n"
        "El sistema está anclado en `AiController`. El disparo (Escape/clic) se traduce a "
        "`is_cancel_input` (solo texto `/cancel`, no teclas). El hueco crítico es "
        "`cancel_level1`: no se ha leído su cuerpo para verificar si aborta la escritura "
        "de archivos en disco.";
    const auto brief3 = tuide::wave_control_brief(packed3);
    expect(brief3.find("cancel_level1") == std::string::npos, "tesis sin función cruda");
    expect(brief3.find("cancel level1") != std::string::npos, "tesis conserva el rol leído");
    expect(brief3.find("is cancel input") != std::string::npos, "tesis conserva is cancel input");
    expect(brief3.find("AiController") == std::string::npos, "tesis sin tipo pegado");
    expect(brief3.find("src/") == std::string::npos && brief3.find("::") == std::string::npos,
           "tesis sin paths");
    expect(brief3.find("\n- cancel\n") == std::string::npos &&
               brief3.find("- cancel\n") == std::string::npos,
           "stem cancel no es rol");
    expect(brief3.find("cómo queda el archivo al cancelar la generación") == std::string::npos,
           "abierto no es receta de archivo");
    const auto limpia = tuide::wave_parse_control(
        R"({"action":"control_v1","do":"explorar","consulta":"cómo aborta cancel_level1 la escritura en disco para evitar archivos a medias","why":"el flujo está trazado; falta la limpieza del archivo"})");
    expect(limpia.ok, "consulta con stem se limpia");
    expect(limpia.consulta.find('_') == std::string::npos, "consulta emitida sin stem");
    expect(limpia.consulta.find("escritura") != std::string::npos, "queda el rol archivo");
    WaveState packed4 = packed;
    packed4.huecos_claimed = {"src/ai/ai_controller.cpp:begin_thinking"};
    packed4.cierre =
        "(El why no es evidencia de lo no leído.)\n"
        "ON src/ai/ai_controller.cpp:begin_insert_at, src/ai/ai_controller.cpp:run_insert_async "
        "via begin_thinking. OFF src/ai/ai_controller.cpp:end_thinking.";
    const auto brief4 = tuide::wave_control_brief(packed4);
    expect(brief4.find("src ai.cpp") == std::string::npos, "tesis sin migas de path");
    expect(brief4.find("Cerrado:\n(vacío)") != std::string::npos, "circuito solo = vacío");
    WaveState packed_visto;
    packed_visto.prompt = packed.prompt;
    packed_visto.opened_ids = {"M8"};
    packed_visto.cierre =
        "Visto: `src/ai/ai_controller.cpp:cancel_current` `src/ai/ai_controller.cpp:cancel_all`\n"
        "Huecos: `src/ai/ai_controller.cpp:handle_user_input`\n"
        "(El why no es evidencia de lo no leído.)\n"
        "ON src/ai/ai_controller.cpp:begin_insert_at via begin_thinking. OFF .";
    const auto brief_visto = tuide::wave_control_brief(packed_visto);
    expect(brief_visto.find("Cerrado:\n(vacío)") == std::string::npos, "visto no se tira");
    expect(brief_visto.find("cancel current") != std::string::npos &&
               brief_visto.find("cancel all") != std::string::npos,
           "visto vira a rol");
    expect(brief_visto.find("src/") == std::string::npos, "visto sin paths");
    WaveState packed_esc;
    packed_esc.prompt = packed.prompt;
    packed_esc.opened_ids = {"M2", "M8"};
    packed_esc.cierre =
        "Visto: `src/ui/console_panel.cpp:handle_ai_console_keys`\nHuecos: (nada)\n"
        "(El why no es evidencia de lo no leído.)\n"
        "La captura de Escape ocurre en `src/ui/console_panel.cpp:handle_ai_console_keys`. "
        "`is_cancel_input` solo ve `/cancel`, no la tecla.";
    const auto brief_esc = tuide::wave_control_brief(packed_esc);
    expect(brief_esc.find("handle ai console keys") != std::string::npos, "tesis: handler");
    expect(brief_esc.find("is cancel input") != std::string::npos, "tesis: is cancel input");
    expect(brief_esc.find("src/") == std::string::npos && brief_esc.find('`') == std::string::npos,
           "tesis escape sin path ni fences");
    expect(brief_esc.find("leído:") != std::string::npos &&
               brief_esc.find("handle ai console keys") != std::string::npos,
           "leído muestra el handler");
    expect(brief_esc.find("el gesto de tecla o clic que cancela la generación") == std::string::npos,
           "escape leído no inventa hueco de tecla");
    WaveState job_a;
    job_a.prompt = "dónde se captura la tecla escape";
    job_a.peeks_done = {"src/ui/console_panel.cpp:handle_ai_console_keys"};
    WavePeekNeighbors neigh;
    neigh.loc = "src/ui/console_panel.cpp:handle_ai_console_keys";
    neigh.callees.push_back({"src/terminal/app_session.cpp:forward_pty_key", "app_session", 1, -1.f});
    job_a.peek_neighbors.push_back(neigh);
    const auto snap_a = tuide::wave_control_job_snap(1, job_a);
    WaveState job_b;
    job_b.prompt = "dónde se aborta el trabajo en curso";
    job_b.peeks_done = {"src/ai/ai_controller.cpp:cancel_current"};
    job_b.follows_done = {"src/ai/ai_controller.cpp:handle_user_input"};
    job_b.circuit_entre =
        "src/ai/ai_controller.cpp:handle_user_input → src/ai/ai_controller.cpp:cancel_current";
    const auto snap_b = tuide::wave_control_job_snap(2, job_b);
    const auto one = tuide::wave_control_jobs_circuit_pack({snap_a});
    expect(one.find("control_opened_v1") != std::string::npos, "pack header");
    expect(one.find("T1") != std::string::npos && one.find("handle ai console keys") != std::string::npos,
           "T1 visto en rol");
    expect(one.find("forward pty key") != std::string::npos, "T1 extra hop");
    expect(one.find("entre abiertas:") == std::string::npos, "un trabajo no pide entre");
    expect(one.find("hacia el resto:") != std::string::npos, "extra no leído va a resto");
    expect(one.find("src/") == std::string::npos && one.find("console_panel") == std::string::npos,
           "pack sin paths");
    const auto two = tuide::wave_control_jobs_circuit_pack({snap_a, snap_b});
    expect(two.find("entre abiertas:") != std::string::npos, "dos trabajos piden entre");
    expect(two.find("T1=>T2") != std::string::npos && two.find("(ningún port)") != std::string::npos,
           "sin registry: ningún port");
    expect(two.find("entre interno:") != std::string::npos &&
               two.find("handle user input") != std::string::npos &&
               two.find("cancel current") != std::string::npos,
           "T2 ya trajo la línea");
    expect(two.find("src/") == std::string::npos, "entre sin paths");
    tuide::WaveControlPathFn linked = [](const std::string& from, const std::string& to,
                                         std::string* md, std::vector<WaveHit>* hops,
                                         std::string* err) {
      if (hops == nullptr) {
        return false;
      }
      const bool a = from.find("handle_ai_console_keys") != std::string::npos ||
                     to.find("handle_ai_console_keys") != std::string::npos;
      const bool b = from.find("cancel_current") != std::string::npos ||
                     to.find("cancel_current") != std::string::npos;
      if (!a || !b) {
        if (err) {
          *err = "sin camino";
        }
        return false;
      }
      WaveHit h1;
      h1.path = "src/ui/console_panel.cpp";
      h1.symbol = "handle_ai_console_keys";
      WaveHit h2;
      h2.path = "src/ai/ai_controller.cpp";
      h2.symbol = "handle_user_input";
      WaveHit h3;
      h3.path = "src/ai/ai_controller.cpp";
      h3.symbol = "cancel_current";
      hops->push_back(h1);
      hops->push_back(h2);
      hops->push_back(h3);
      if (md) {
        *md = "ok";
      }
      return true;
    };
    const auto bridged = tuide::wave_control_jobs_circuit_pack({snap_a, snap_b}, linked);
    expect(bridged.find("T1=>T2") != std::string::npos &&
               bridged.find("handle ai console keys") != std::string::npos &&
               bridged.find("cancel current") != std::string::npos,
           "registry pinta el puente");
    expect(bridged.find("sin camino") == std::string::npos, "con camino no dice sin");
    expect(bridged.find("src/") == std::string::npos, "puente sin paths");
    tuide::WaveControlPathFn none = [](const std::string&, const std::string&, std::string*,
                                       std::vector<WaveHit>*, std::string* err) {
      if (err) {
        *err = "sin camino";
      }
      return false;
    };
    const auto cut = tuide::wave_control_jobs_circuit_pack({snap_a, snap_b}, none);
    expect(cut.find("sin camino") != std::string::npos, "registry vacío = sin camino");
    WaveState share_a = job_a;
    share_a.peeks_done.push_back("src/ai/ai_controller.cpp:cancel_current");
    share_a.peek_neighbors[0].callees.push_back({"src/ai/ai_trace.cpp:append", "ai_trace", 1, -1.f});
    share_a.peek_neighbors[0].callees.push_back({"src/ai/ai_controller.cpp:L10:if", "", 1, -1.f});
    WaveState share_b = job_b;
    const auto share_pack =
        tuide::wave_control_jobs_circuit_pack({tuide::wave_control_job_snap(1, share_a),
                                               tuide::wave_control_job_snap(2, share_b)});
    expect(share_pack.find("mismo objeto:") != std::string::npos &&
               share_pack.find("cancel current") != std::string::npos,
           "visto compartido gana");
    expect(share_pack.find("(ningún port)") == std::string::npos &&
               share_pack.find("sin camino") == std::string::npos,
           "compartido no finge hueco");
    expect(share_pack.find("extra: append") == std::string::npos &&
               share_pack.find(" → if") == std::string::npos &&
               share_pack.find("extra: min") == std::string::npos,
           "hops ctrl no salen");
    expect(share_pack.find("forward pty key") != std::string::npos, "hop útil se queda");
    tuide::WaveControlPathFn junk = [](const std::string& from, const std::string& to,
                                       std::string* md, std::vector<WaveHit>* hops,
                                       std::string* err) {
      if (hops == nullptr) {
        return false;
      }
      WaveHit h1;
      h1.path = "src/ui/console_panel.cpp";
      h1.symbol = "handle_ai_console_keys";
      WaveHit ctrl;
      ctrl.kind = "ctrl";
      ctrl.symbol = "if";
      WaveHit app;
      app.path = "src/ai/ai_trace.cpp";
      app.symbol = "append";
      WaveHit h2;
      h2.path = "src/ai/ai_controller.cpp";
      h2.symbol = "cancel_level1";
      hops->push_back(h1);
      hops->push_back(ctrl);
      hops->push_back(app);
      hops->push_back(h2);
      if (md) {
        *md = "ok";
      }
      (void)from;
      (void)to;
      (void)err;
      return true;
    };
    WaveState only_a;
    only_a.peeks_done = {"src/ui/console_panel.cpp:handle_ai_console_keys"};
    WaveState only_b;
    only_b.peeks_done = {"src/ai/ai_controller.cpp:cancel_level1"};
    const auto cleaned = tuide::wave_control_jobs_circuit_pack(
        {tuide::wave_control_job_snap(1, only_a), tuide::wave_control_job_snap(2, only_b)}, junk);
    expect(cleaned.find("handle ai console keys") != std::string::npos &&
               cleaned.find("cancel level1") != std::string::npos,
           "puente sin ctrl");
    expect(cleaned.find(" → if") == std::string::npos && cleaned.find("append") == std::string::npos,
           "if/append fuera del puente");
    expect(tuide::wave_control_system_prompt().find("hops extra") != std::string::npos,
           "prompt: foto hops extra");
    expect(tuide::wave_control_system_prompt().find("hijo cierra SU consulta") != std::string::npos,
           "prompt: hijo vs ancla");
    expect(tuide::wave_control_system_prompt().find("el ancla ya tiene respuesta") == std::string::npos,
           "ejemplo cerrar no finge el ancla");
    expect(tuide::wave_control_system_prompt().find("el hijo no la lee") != std::string::npos,
           "consulta olor; why estrategia");
    expect(tuide::wave_control_system_prompt().find("\"hacia\"") != std::string::npos,
           "ejemplo explorar con hacia");
    expect(tuide::wave_control_system_prompt().find("Un explorar es una sola rama") != std::string::npos &&
               tuide::wave_control_system_prompt().find("un solo locus") != std::string::npos &&
               tuide::wave_control_system_prompt().find("dos loci") != std::string::npos,
           "prompt: una rama, un locus");
    expect(tuide::wave_control_system_prompt().find("un locator cada") == std::string::npos,
           "prompt no receta un locator por objeto");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n")
               .find("tú eliges cómo seguir") != std::string::npos,
           "user: mezcla se rechaza; él dirige el siguiente paso");
    const auto exam = tuide::wave_control_slice_exam(
        "cuando pulsa Escape o clic fuera, cancelar sin archivo a medias", {snap_a, snap_b});
    expect(exam.find("# examen") != std::string::npos && exam.find("Ancla:") != std::string::npos,
           "examen header");
    expect(exam.find("T1 preguntó:") != std::string::npos &&
               exam.find("dónde se captura la tecla escape") != std::string::npos,
           "examen pregunta T1");
    expect(exam.find("Cerrado de T2:") != std::string::npos, "examen cerrado T2");
    expect(exam.find("src/") == std::string::npos, "examen sin paths");
    tuide::WaveControlJobSnap snap_deny = snap_a;
    snap_deny.cerrado =
        "ai_trace_escape es sanitización de strings, NO la captura del evento. El nombre es "
        "engañoso.";
    const auto exam_deny = tuide::wave_control_slice_exam("ancla", {snap_deny});
    expect(exam_deny.find("No se encontró el objeto de esta caza") != std::string::npos &&
               exam_deny.find("No la relances") != std::string::npos &&
               exam_deny.find("no son el siguiente encargo") != std::string::npos,
           "examen: huecos del fallo no son el siguiente encargo");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, two,
                                          exam)
               .find("Tú diriges:") != std::string::npos,
           "user pide dirigir, no receta de cobertura");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, two,
                                          exam)
               .find("# examen") != std::string::npos,
           "user lleva examen pegado");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, two)
               .find("Lee Cerrado, leído y el circuito") == std::string::npos,
           "con trabajos no empuja a cerrar por circuito");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, two)
               .find("entre abiertas:") != std::string::npos,
           "user lleva entre abiertas");
    expect(tuide::wave_control_system_prompt().find("ampliar") != std::string::npos,
           "piloto puede ampliar");
    expect(tuide::wave_control_system_prompt().find("PRIMERO") != std::string::npos,
           "ampliar si duda");
    expect(tuide::wave_control_system_prompt().find("briefing de caza") != std::string::npos &&
               tuide::wave_control_system_prompt().find("no ve el ancla") != std::string::npos,
           "consulta es briefing; el hijo no ve el ancla");
    expect(tuide::wave_control_system_prompt().find("enumera") != std::string::npos &&
               tuide::wave_control_system_prompt().find("solo con el ancla") != std::string::npos,
           "prompt: no recap; no heredar la enumeración");
    expect(tuide::wave_control_system_prompt().find("gesto de puntero") == std::string::npos &&
               tuide::wave_control_system_prompt().find("se escribe al lanzar") != std::string::npos,
           "plan: B no congela consulta; no receta de dos disparos");
    expect(tuide::wave_control_system_prompt().find("rima con el ancla") != std::string::npos,
           "few-shot descarta hole que rima");
    expect(tuide::wave_control_system_prompt().find("no inventes el locator") != std::string::npos,
           "no inventar locator desde el ancla");
    expect(tuide::wave_control_system_prompt().find("el panel cubre") == std::string::npos,
           "ejemplo sin clavar el panel");
    expect(tuide::wave_control_system_prompt().find("Chrome no es el disparo") != std::string::npos,
           "chrome no es el disparo");
    expect(tuide::wave_control_system_prompt().find("\"do\":\"plan\"") != std::string::npos &&
               tuide::wave_control_system_prompt().find("no de una receta") != std::string::npos,
           "prompt: plan genérico");
    expect(tuide::wave_control_system_prompt().find("DESCOMPONES") == std::string::npos &&
               tuide::wave_control_system_prompt().find("PLAN primero") == std::string::npos,
           "prompt no fuerza descomponer");
    expect(tuide::wave_control_system_prompt().find("cazador estrecho") != std::string::npos &&
               tuide::wave_control_system_prompt().find("Atomiza el trabajo") != std::string::npos,
           "prompt: atomiza el grano; el hijo pierde filo si mezclas");
    expect(tuide::wave_control_system_prompt().find("mapa no une") != std::string::npos,
           "prompt: plan si el mapa no une");
    expect(tuide::wave_control_system_prompt().find("pulsaciones de teclado") == std::string::npos,
           "ejemplo sin receta de teclas");
    expect(tuide::wave_control_system_prompt().find("bosquejar") == std::string::npos &&
               tuide::wave_control_system_prompt().find("\"do\":\"bosquejar\"") == std::string::npos,
           "bosquejo off: sin few-shot");
    expect(tuide::wave_control_system_prompt().find("\"do\":\"ampliar\"") != std::string::npos,
           "few-shot ampliar fichas");
    expect(tuide::wave_control_system_prompt().find("\"do\":\"zoom\"") == std::string::npos,
           "censo off: sin few-shot zoom de barrio");
    expect(tuide::wave_control_system_prompt().find("\"do\":\"agujas\"") == std::string::npos,
           "censo off: sin few-shot agujas");
    expect(tuide::wave_control_system_prompt().find("olores de zona") == std::string::npos,
           "bosquejo off: sin olores");
    expect(tuide::wave_control_system_prompt().find("keyboard") == std::string::npos &&
               tuide::wave_control_system_prompt().find("escape") == std::string::npos &&
               tuide::wave_control_system_prompt().find("tecla") == std::string::npos,
           "bosquejar sin receta de gesto");
    expect(tuide::wave_control_user_prompt("ancla", "").find("Aún no hay exploradores") !=
               std::string::npos,
           "primer turno sin exploradores");
    expect(tuide::wave_control_user_prompt("ancla", "").find("amplia 1–3 M*") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "").find("primero agujas") ==
                   std::string::npos,
           "primer turno: fichas y ampliar, no censo");
    expect(tuide::wave_control_user_prompt("ancla", "", "", "", {}, {}, "", "",
                                          tuide::WaveControlCue::Default, "",
                                          "luz (calibración del piloto; no copies): event, mouse\n"
                                          "plano (calor del ancla)\n  ui ●●●\n")
               .find("ya tiene luz") == std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "", "", "", {}, {}, "", "",
                                              tuide::WaveControlCue::Default, "",
                                              "luz (calibración del piloto; no copies): event, mouse\n"
                                              "plano (calor del ancla)\n  ui ●●●\n")
                   .find("plano (calor del ancla)") == std::string::npos,
           "censo off: el user no ve el plano");
    const auto after_zoom = tuide::wave_control_user_prompt(
        "ancla", "", "", "", {}, {}, "", "", tuide::WaveControlCue::Default, "",
        "luz (calibración del piloto; no copies): event, mouse\n"
        "plano (calor del ancla)\n  ui ●●●\n",
        "zoom ui\n  console_panel\n");
    expect(after_zoom.find("Ya hay zoom") == std::string::npos &&
               after_zoom.find("amplia 1–3 M*") != std::string::npos,
           "censo off: zoom de barrio no cambia el cue");
    expect(tuide::wave_control_user_prompt("ancla", "", "", "", {}, {}, "", "",
                                          tuide::WaveControlCue::Default, "",
                                          "plano (calor del ancla)\n  ui ·  teclas\n")
               .find("plano (calor del ancla)") == std::string::npos,
           "censo off: plano no entra en el user");
    expect(tuide::wave_control_user_prompt("ancla", "", "", "", {}, {}, {}, {},
                                          tuide::WaveControlCue::Default, foto_md)
               .find("escape ~ ui") != std::string::npos,
           "user ve la foto");
    expect(tuide::wave_control_user_prompt("ancla", "").find("Si el mapa no une") == std::string::npos,
           "user no obliga plan");
    expect(tuide::wave_control_user_prompt("ancla", "").find("amplia 1–3 M*") !=
               std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "").find("briefing de caza") !=
                   std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "").find("no enumera") !=
                   std::string::npos,
           "user ofrece ampliar fichas, explorar o plan");
    expect(tuide::wave_control_user_prompt("ancla", "").find("Legal ahora:") != std::string::npos,
           "user lista do legales");
    expect(tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
               .find("objeto y verbo") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
                   .find("no el ancla con") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
                   .find("primer locator") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
                   .find("Atomiza") != std::string::npos,
           "tras ampliar: una caza o un plan, no recap");
    const auto after_inspect =
        tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel");
    expect(after_inspect.find("Cierras esto (no lo recopies)") != std::string::npos &&
               after_inspect.find("pack") < after_inspect.find("Cierras esto"),
           "tras inspect el ancla va al final");
    expect(tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
               .find("no otra vez") == std::string::npos,
           "no veta un segundo ampliar");
    expect(tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
               .find("Chrome no es el disparo") != std::string::npos,
           "tras ampliar chrome no clava");
    expect(tuide::wave_control_user_prompt("ancla", "", "pack", "M7 owns: panel")
               .find("entre abiertas son ports") != std::string::npos,
           "tras ampliar: ports no cosine");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n")
               .find("Tú diriges:") != std::string::npos,
           "user pide dirigir tras un trabajo");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\nCerrado:\nno es eso\n")
               .find("caza falló") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\nCerrado:\nno es eso\n")
                       .find("no son el siguiente encargo") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\nCerrado:\nno es eso\n")
                       .find("sale de Abierto") == std::string::npos,
           "user: fallo no empuja Abierto como siguiente briefing");
    expect(tuide::wave_control_system_prompt().find("no el siguiente encargo") != std::string::npos &&
               tuide::wave_control_system_prompt().find("Cambia de objeto, no de nombre") !=
                   std::string::npos,
           "system: huecos del fallo no son el siguiente encargo");
    expect(tuide::wave_control_system_prompt().find("mismo objeto") != std::string::npos,
           "system: mismo objeto no es un deber");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n")
               .find("el hijo no la lee") != std::string::npos,
           "user separa why de consulta");
    expect(tuide::wave_control_system_prompt(tuide::WaveControlCue::Neutral)
                   .find("hijo cierra SU consulta") == std::string::npos &&
               tuide::wave_control_system_prompt(tuide::WaveControlCue::Neutral)
                       .find("al ancla le queda otra cosa") == std::string::npos,
           "Neutral: sin receta hijo vs ancla");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, {},
                                          "", tuide::WaveControlCue::Neutral)
                   .find("Si ves un objeto del ancla") == std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {},
                                              {}, "", tuide::WaveControlCue::Neutral)
                       .find("El Cerrado responde a la pregunta") == std::string::npos,
           "Neutral: user no empuja a cubrir el ancla");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n")
                   .find("Si ves un objeto del ancla") == std::string::npos,
           "Default: sin receta de cobertura del ancla");
    expect(tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {}, {},
                                          "# examen\nAncla:\nx\n", tuide::WaveControlCue::Neutral)
                   .find("# examen") != std::string::npos &&
               tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nkeep: M8\n", "", "", {}, {},
                                              {}, "", tuide::WaveControlCue::Neutral)
                       .find("JSON ahora") != std::string::npos,
           "Neutral: foto y JSON siguen");
    tuide::WaveControlPlan prompt_plan = board;
    const auto gate_md = tuide::wave_control_user_prompt("ancla", "### Trabajo 1\nCerrado:\nhola\n",
                                                        "", "", prompt_plan, t0b);
    expect(gate_md.find("[paso]") != std::string::npos, "tablero marca paso");
    expect(gate_md.find("Legal ahora:") != std::string::npos &&
               gate_md.find("pasar") != std::string::npos,
           "puerta lista pasar");
    expect(gate_md.find("Cerrado afirma") != std::string::npos, "pasar si Cerrado afirma");
    prompt_plan.fases[0].status = tuide::WaveControlPhaseStatus::Fallo;
    const auto fail_md = tuide::wave_control_user_prompt("ancla", "### Trabajo 1\n", "", "",
                                                        prompt_plan, tfail);
    expect(fail_md.find("[fallo]") != std::string::npos, "tablero marca fallo");
    const auto atb = tuide::wave_control_atlas_brief(
        "# causal_atlas_v1\nview: atlas\nsearch: cancel_generation\n"
        "M8  kind=latch  ov=6  stems: ai_controller\n    owns: ai_controller\n"
        "    peek: ai_controller::cancel_level1\n    port: M8=>M6 foo\n");
    expect(atb.find("M8") != std::string::npos, "atlas brief M8");
    expect(atb.find("ai controller") != std::string::npos, "owns sin stem crudo");
    expect(atb.find("cancel level1") == std::string::npos && atb.find("peek") == std::string::npos,
           "brief sin peek");
    expect(atb.find('_') == std::string::npos, "brief sin underscore");
    const auto pack_at = tuide::wave_control_atlas_pack_brief(
        "# causal_atlas_v1\nview: atlas\nsearch: cancel_generation\n"
        "M2  kind=caller  ov=6  stems: ai_trace\n    owns: ai_trace\n"
        "    peek: ai_trace::ai_trace_escape\n"
        "    port: M2=>M3 level2_session::json_escape\n"
        "M5  kind=caller  ov=0\n    peek: console_panel::handle_ai_console_keys\n"
        "holes: raw_pty_screen::skip_escape\n");
    expect(pack_at.find("ai trace escape") != std::string::npos, "atlas pack conserva peek");
    expect(pack_at.find("handle ai console keys") != std::string::npos, "atlas pack M5 peek");
    expect(pack_at.find("skip escape") != std::string::npos, "atlas pack huecos con peek");
    expect(pack_at.find("::") == std::string::npos, "atlas pack sin ::");
    expect(pack_at.find("search:") == std::string::npos, "atlas pack sin search");
    std::string fat_rest = pack_at;
    for (int i = 0; i < 40; ++i) {
      fat_rest += "M99  kind=caller  ov=0\n    peek: filler_panel::noise_entry_point\n";
    }
    const auto rest_cap =
        tuide::wave_control_atlas_pack_brief(fat_rest, tuide::kWaveControlRestChars);
    expect(rest_cap.size() <= static_cast<std::size_t>(tuide::kWaveControlRestChars) + 160,
           "resto del atlas se compacta; no el inspect");
    const auto mmap = tuide::wave_control_module_map("/home/lorenzo/workspace/TIDE");
    expect(mmap.find("ai") != std::string::npos, "mapa tiene ai");
    expect(mmap.find("ui") != std::string::npos, "mapa tiene ui");
    expect(mmap.find("src/") == std::string::npos, "mapa sin paths");
    nlohmann::json cards = nlohmann::json::parse(R"({
      "zones": [
        {"id":"M8","kind":"latch","primary_stems":["ai_controller"],
         "nuclei":[{"state":"pending_insert_"}],
         "representatives":[{"target":"src/ai/ai_controller.cpp:cancel_current"}]},
        {"id":"M2","kind":"caller","primary_stems":["ai_trace"],
         "representatives":[{"target":"src/ai/ai_trace.cpp:ai_trace_escape"}]},
        {"id":"M7","kind":"chrome","primary_stems":["editor_panel"],
         "nuclei":[{"state":"scroll"}],
         "representatives":[{"target":"src/ui/editor_panel.cpp:handle"}]},
        {"id":"M11","risks":["uncovered_candidate"],"primary_stems":["git_service"],
         "representatives":[{"target":"src/git/git_service.cpp:cancel_remote"}]}
      ]
    })");
    const auto barrios =
        tuide::wave_control_barrio_brief(cards, "cancelar generación", "/home/lorenzo/workspace/TIDE");
    expect(barrios.find("ai  ov=") != std::string::npos, "barrio ai");
    expect(barrios.find("M8") != std::string::npos, "M8 bajo barrio");
    expect(barrios.find("M8  kind=latch  ov=0") == std::string::npos, "M8 ov por peek oculto");
    expect(barrios.find("kind=latch") != std::string::npos, "latch gana el recorte");
    expect(barrios.find("M7") != std::string::npos, "M7 bajo barrio");
    expect(barrios.find("kind=chrome") != std::string::npos, "panel es chrome no latch");
    expect(barrios.find("Huecos de retrieval") != std::string::npos, "huecos aparte");
    expect(barrios.find("git  ov=") == std::string::npos, "git hole no es barrio");
    expect(barrios.find("También en el repo") != std::string::npos, "resto de src");
    expect(barrios.find("src/") == std::string::npos, "barrios sin paths");
    const auto resto =
        tuide::wave_control_barrio_brief(cards, "cancelar generación", "/home/lorenzo/workspace/TIDE",
                                         {"M8"});
    expect(resto.find("Resto compacto") != std::string::npos, "resto tras ampliar");
    expect(resto.find("M8  kind=") == std::string::npos, "abierta no se repite compacta");
    expect(resto.find("M2") != std::string::npos, "resto tiene la ficha no abierta");
    expect(resto.find("M7") != std::string::npos, "resto sigue mostrando ui");
    const auto opened = tuide::wave_control_opened_brief(
        "# pilot_opened_v1\nn=2  (owns+nucleus+peek+circuito; sin inspect gordo)\n\n"
        "M8  kind=latch  ov=6\n"
        "    owns: ai_controller\n"
        "    nucleus: pending_insert_\n"
        "    peek: ai_controller::cancel_current, ai_controller::begin_insert_at\n"
        "entre abiertas:\n"
        "  M2=>M8 handle_route -call-> ai_trace_escape\n"
        "hacia el resto:\n"
        "  M8=>M6 cancel_level1 -call-> load\n"
        "# causal_judge_v1\nquery: cancel\ngate: cosine=0.1\n"
        "## M8\n"
        "stems: shutdown overlay editor text level1 action\n"
        "mechanism:\n"
        "- trigger: ai_controller::cancel_current -enter_ctrl-> pending_insert_\n"
        "mini-cards:\n"
        "- ai_controller::cancel_current | void cancel_current()\n"
        "  · 753: clear_pending_insert();\n"
        "M7  kind=chrome  ov=1\n"
        "    owns: editor_panel\n"
        "    nucleus: scroll\n"
        "    peek: editor_panel::handle_breadcrumb_click, editor_panel::clear_hover_state\n");
    expect(opened.find("pending insert") != std::string::npos, "opened nucleus en rol");
    expect(opened.find("cancel current") != std::string::npos, "opened peek en rol");
    expect(opened.find("stems:") == std::string::npos, "opened sin dump de stems");
    std::string fat = "# pack\nM3  kind=caller\n    owns: loop\n## M3\nkind=caller\nowns: loop\n";
    for (int i = 0; i < 120; ++i) {
      fat += "mini-cards: filler line of inspect noise that used to eat the last card\n";
    }
    fat += "## M10\nkind=hole\nowns: ai_controller\npeek: ai_controller::begin_download\n";
    const auto split = tuide::wave_control_opened_brief(fat);
    expect(split.find("## M10") != std::string::npos &&
               split.find("ai controller") != std::string::npos,
           "inspect reparte presupuesto: la última ficha no se corta");
    expect(opened.find("entre abiertas:") != std::string::npos &&
               opened.find("M2=>M8") != std::string::npos &&
               opened.find("handle route") != std::string::npos,
           "opened conserva circuito entre fichas");
    expect(opened.find("hacia el resto:") != std::string::npos &&
               opened.find("cancel level1") != std::string::npos,
           "opened conserva ports hacia el resto");
    expect(opened.find("breadcrumb click") != std::string::npos, "opened peek chrome");
    expect(opened.find("## M8") != std::string::npos, "inspect ## se conserva");
    expect(opened.find("mechanism:") != std::string::npos, "inspect mechanism");
    expect(opened.find("clear pending insert") != std::string::npos, "outline en rol");
    expect(opened.find("causal judge") == std::string::npos, "sin título #");
    expect(opened.find("query:") == std::string::npos, "sin query de judge");
    expect(opened.find("::") == std::string::npos, "opened sin ::");
    expect(opened.find('_') == std::string::npos, "opened sin underscore");
    expect(opened.find("src/") == std::string::npos, "opened sin paths");
    const auto insp = tuide::wave_control_inspect_brief(cards, {"M7"}, "cancelar generación");
    expect(insp.find("M7") != std::string::npos, "inspect M7");
    expect(insp.find("núcleo: scroll") != std::string::npos, "inspect núcleo no eco");
    expect(insp.find("not:") != std::string::npos, "inspect not");
    expect(insp.find("latch y disparo") != std::string::npos, "chrome no es el disparo");
    expect(insp.find("owns:") == std::string::npos, "inspect sin owns para no copiarlo");
    expect(insp.find("editor panel") == std::string::npos, "inspect sin etiqueta de owns");
    expect(insp.find("peek") == std::string::npos, "inspect sin peek");
    expect(insp.find("src/") == std::string::npos, "inspect sin paths");
    expect(insp.find('_') == std::string::npos, "inspect sin underscore");
    const auto insp8 = tuide::wave_control_inspect_brief(cards, {"M8"}, "cancelar generación");
    expect(insp8.find("pending insert") != std::string::npos, "latch núcleo pending");
    expect(insp8.find("gesto de tecla") != std::string::npos, "latch not = gesto");
    const auto owns = tuide::wave_control_owns_phrases(cards);
    const auto stripped = tuide::wave_control_strip_owns(
        "dónde se captura la tecla Escape o el clic fuera en el editor panel", owns);
    expect(stripped.find("editor") == std::string::npos && stripped.find("panel") == std::string::npos,
           "strip owns del locus");
    expect(stripped.find("tecla") != std::string::npos, "strip deja el gesto");
    expect(tuide::wave_control_user_prompt("ancla", "", barrios, insp).find("M7") != std::string::npos,
           "user lleva inspect");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_wave_test ok\n";
  return 0;
}
