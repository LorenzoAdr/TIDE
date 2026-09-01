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
    expect(tuide::wave_needs_cover(seeded), "cover en ola 0");
    const std::string seed_nb = tuide::wave_notebook_markdown(seeded);
    expect(seed_nb.find("files:") != std::string::npos, "notebook files");
    expect(seed_nb.find("busy_strip.hpp") != std::string::npos, "notebook header");
    std::string err;
    const auto juicio = wave_parse_ola(
        R"({"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M2"],"why":"el latch es el LED, chrome no"})");
    expect(wave_apply(&seeded, juicio, ops, &err), "juicio sobre atlas sin needles");
    expect(!tuide::wave_needs_cover(seeded), "tras juicio no cover");
    tuide::wave_retain_atlas_ids(&seeded, {"M1"});
    expect(tuide::wave_find_hit(seeded.candidatas, "M1") != nullptr, "retain M1");
    expect(tuide::wave_find_hit(seeded.candidatas, "M2") == nullptr, "drop atlas M2");
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
      expect(body.find("set_busy_spinner") != std::string::npos ||
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
    expect(causal_n == 1, "función llama peek_causal");
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
    expect(tuide::wave_work_markdown(fs).find("Siguiente legal") != std::string::npos,
           "hint follow repetido");
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
    expect(tuide::wave_work_markdown(is).find("Siguiente legal") != std::string::npos,
           "user hint tras in repetido");
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
    expect(cover.find("PROHIBIDO drop del caller") != std::string::npos, "cover no drop caller");
    expect(cover.find("latch + caller") != std::string::npos, "cover latch y caller");
    const auto salvaged = tuide::wave_salvage_keep(
        "Task recap...\n{\"action\":\"ola_v1\",\"do\":\"juicio\",\"keep\":[\"M1\",\"M7\"]",
        {"M1", "M2", "M7"});
    expect(salvaged.size() == 2, "salvage keep truncado");
    expect(salvaged[0] == "M1" && salvaged[1] == "M7", "salvage M1 M7");
    const std::string pilot = tuide::wave_pilot_system_prompt();
    expect(pilot.find("tanda") != std::string::npos, "pilot tanda");
    expect(pilot.find("Aguas mismo-stem") != std::string::npos, "pilot aguas incompletas");
    expect(pilot.find("bosquejo") != std::string::npos ||
               pilot.find("sin arista") != std::string::npos,
           "pilot bosquejo islas");
    expect(pilot.find("stems keep") != std::string::npos, "pilot peek vecinos");
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
    expect(pilot.find("1 latch + 1 caller") != std::string::npos, "pilot tanda latch+caller");
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
    expect(tuide::wave_pilot_user_prompt(last_p).find("Cierra si ya entendiste") != std::string::npos,
           "última ola deja cerrar al piloto");
    WaveState circuit;
    circuit.prompt = last.prompt;
    circuit.circuit_on = {"src/ai/ai_controller.cpp:begin_thinking"};
    circuit.circuit_off = {"src/ai/ai_controller.cpp:end_thinking"};
    const std::string up2 = tuide::wave_pilot_user_prompt(circuit);
    expect(up2.find("Circuito ON y OFF anclado") != std::string::npos, "user cita circuito");
    expect(up2.find("no vallado") != std::string::npos, "circuito no es barrera");
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
    expect(work.find("islas:") != std::string::npos, "begin y end son islas");
    {
      const auto p = work.find("islas:");
      const auto n = work.find('\n', p);
      const std::string line = work.substr(p, n == std::string::npos ? work.size() - p : n - p);
      expect(line.find("`M1`") == std::string::npos, "islas sin alias M1");
    }
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
    expect(md2.find("islas:") != std::string::npos && md2.find("end_thinking") != std::string::npos,
           "end_thinking isla sin arista al latch");
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
      const auto nl = rk.notas.find('\n', calls_at);
      const std::string line =
          rk.notas.substr(calls_at, nl == std::string::npos ? rk.notas.size() - calls_at : nl - calls_at);
      expect(line.find("ensure_spinner_thread") != std::string::npos, "calls ensure");
      expect(line.find("i18n") == std::string::npos, "calls sin i18n");
      expect(line.find("_ms") == std::string::npos, "calls sin _ms");
      expect(line.find("paint_") == std::string::npos, "calls sin paint_");
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
    expect(tuide::wave_cover_restore_caller(&drop_caller, cov) == 1, "restore caller");
    bool keep_m1 = false;
    bool keep_m7 = false;
    for (const auto& id : drop_caller.keep) {
      keep_m1 = keep_m1 || id == "M1";
      keep_m7 = keep_m7 || id == "M7";
    }
    expect(keep_m1 && keep_m7, "keep latch y caller");
    bool drop_m7 = false;
    for (const auto& id : drop_caller.drop) {
      drop_m7 = drop_m7 || id == "M7";
    }
    expect(!drop_m7, "caller sale de drop");
    std::string err;
    expect(wave_apply(&cov, drop_caller, ops, &err), "apply cover restore");
    bool zona_m7_keep = false;
    for (const auto& z : cov.zonas) {
      if (z.id == "M7" && z.verdict == "keep") {
        zona_m7_keep = true;
      }
    }
    expect(zona_m7_keep, "zona caller keep");
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
    expect(up.find("1 latch + 1 caller") != std::string::npos, "user tanda tras cover");
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

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_wave_test ok\n";
  return 0;
}
