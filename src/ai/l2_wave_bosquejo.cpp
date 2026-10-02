#include "ai/l2_wave_bosquejo.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace tuide {
namespace {

std::string ascii_fold_term(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 128) {
      out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
      ++i;
      continue;
    }
    char mapped = 0;
    if (i + 1 < s.size() && c == 0xc3) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d == 0xa1 || d == 0xa0) {
        mapped = 'a';
      } else if (d == 0xa9 || d == 0xa8) {
        mapped = 'e';
      } else if (d == 0xad || d == 0xac) {
        mapped = 'i';
      } else if (d == 0xb3 || d == 0xb2) {
        mapped = 'o';
      } else if (d == 0xba || d == 0xb9 || d == 0xbc) {
        mapped = 'u';
      } else if (d == 0xb1) {
        mapped = 'n';
      }
    }
    if (mapped != 0) {
      out.push_back(mapped);
      i += 2;
    } else {
      ++i;
    }
  }
  return out;
}

std::vector<std::string> split_words(const std::string& folded) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 2) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (char c : folded) {
    if (c == ' ' || c == '\t') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  return out;
}

bool concept_stopword(const std::string& t) {
  static const char* k[] = {"a", "al", "de", "del", "el", "la", "los", "las", "un", "una",
                            "en", "y", "o", "que", "con", "por", "para", "the", "of", "to"};
  for (const char* s : k) {
    if (t == s) {
      return true;
    }
  }
  return false;
}

// Lexicón de zona, no de objetos TIDE: clase de sitio → tokens que el grafo suele tener.
std::vector<std::string> token_aliases(const std::string& tok) {
  if (tok == "tecla" || tok == "teclado" || tok == "keyboard") {
    return {"keys", "key", "keyboard"};
  }
  if (tok == "entrada" || tok == "input") {
    return {"input"};
  }
  if (tok == "evento" || tok == "event") {
    return {"event"};
  }
  if (tok == "clic" || tok == "click" || tok == "puntero" || tok == "mouse") {
    return {"click", "mouse", "pointer"};
  }
  if (tok == "fuera" || tok == "outside") {
    return {"outside"};
  }
  if (tok == "overlay" || tok == "modal" || tok == "capa") {
    return {"overlay", "modal", "popup"};
  }
  if (tok == "archivo" || tok == "file") {
    return {"file"};
  }
  if (tok == "generacion" || tok == "generation") {
    return {"generation"};
  }
  if (tok == "escritura" || tok == "write") {
    return {"write"};
  }
  if (tok == "medias") {
    return {"pending", "insert"};
  }
  if (tok == "cancelacion" || tok == "cancel") {
    return {"cancel"};
  }
  if (tok == "parada" || tok == "abortar") {
    return {"stop", "abort", "cancel"};
  }
  if (tok == "trabajo" || tok == "job") {
    return {"job", "work", "task"};
  }
  return {};
}

std::vector<std::vector<std::string>> phrase_groups(const std::string& term) {
  std::vector<std::vector<std::string>> groups;
  for (const auto& w : split_words(ascii_fold_term(term))) {
    if (concept_stopword(w) || w.size() < 4) {
      continue;
    }
    std::vector<std::string> g;
    g.push_back(w);
    for (const auto& a : token_aliases(w)) {
      if (std::find(g.begin(), g.end(), a) == g.end()) {
        g.push_back(a);
      }
    }
    groups.push_back(std::move(g));
  }
  return groups;
}

std::vector<std::string> concept_queries(const std::string& term) {
  std::vector<std::string> out;
  const std::string folded = ascii_fold_term(term);
  if (folded.size() >= 2) {
    out.push_back(folded);
  }
  for (const auto& g : phrase_groups(term)) {
    for (const auto& q : g) {
      if (std::find(out.begin(), out.end(), q) == out.end()) {
        out.push_back(q);
      }
    }
  }
  return out;
}

std::string english_probe_phrase(const std::string& term) {
  std::string out;
  for (const auto& g : phrase_groups(term)) {
    const std::string& use = g.size() > 1 ? g[1] : g[0];
    if (!out.empty()) {
      out += ' ';
    }
    out += use;
  }
  return out;
}

bool phrase_needs_embed(const std::string& term) {
  const std::string folded = ascii_fold_term(term);
  if (folded.find(' ') != std::string::npos) {
    return true;
  }
  return folded.size() != term.size();
}

bool handler_first_token(const std::string& t) {
  static const char* k[] = {"make", "handle", "is", "on", "update", "set", "get", "has",
                            "begin", "end", "tick", "render", "apply", "open", "close",
                            "clear", "cancel", "bind", "create", "show", "hide"};
  for (const char* s : k) {
    if (t == s) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> symbol_tokens(const std::string& symbol) {
  std::vector<std::string> toks;
  std::string cur;
  auto flush = [&]() {
    if (!cur.empty()) {
      toks.push_back(cur);
      cur.clear();
    }
  };
  for (std::size_t i = 0; i < symbol.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(symbol[i]);
    if (c == '_' || c == ':' || c == '/') {
      flush();
      continue;
    }
    if (std::isupper(c) != 0 && !cur.empty()) {
      flush();
    }
    if (std::isalnum(c) == 0) {
      flush();
      continue;
    }
    cur.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }
  flush();
  return toks;
}

// Exacto / prefijo / primer token: fuerte. Último token solo si el primero es handler
// (MakeFooOverlay sí; json_escape no).
float symbol_anchor_score(const std::string& symbol, const std::string& q) {
  const std::string s = ascii_fold_term(symbol);
  const std::string qq = ascii_fold_term(q);
  if (qq.size() < 2 || s.empty()) {
    return 0.f;
  }
  if (s == qq) {
    if (s.size() <= 8) {
      return 0.f;
    }
    return 3.f;
  }
  if (s.size() > qq.size() && s.compare(0, qq.size(), qq) == 0) {
    const char next = s[qq.size()];
    if (next == '_' || next == ':' || qq.size() >= 4) {
      return 2.f;
    }
  }
  const auto toks = symbol_tokens(symbol);
  if (toks.empty()) {
    return 0.f;
  }
  auto tok_hit = [&](const std::string& tok) {
    if (tok == qq) {
      return true;
    }
    return qq.size() >= 4 && tok.size() > qq.size() && tok.compare(0, qq.size(), qq) == 0;
  };
  if (tok_hit(toks.front())) {
    return 2.f;
  }
  for (std::size_t i = 1; i + 1 < toks.size(); ++i) {
    if (tok_hit(toks[i])) {
      return 1.2f;
    }
  }
  if (toks.size() >= 2 && handler_first_token(toks.front()) && tok_hit(toks.back())) {
    return 1.f;
  }
  return 0.f;
}

int groups_matched(const std::string& symbol, const std::vector<std::vector<std::string>>& groups) {
  int n = 0;
  for (const auto& g : groups) {
    for (const auto& q : g) {
      if (symbol_anchor_score(symbol, q) > 0.f) {
        ++n;
        break;
      }
    }
  }
  return n;
}

std::string barrio_of_hit(const RegistryNodeRow& n) {
  std::string b = wave_control_barrio_of_path(n.path);
  if (b.empty()) {
    b = wave_control_barrio_of_path(n.id);
  }
  if (b.empty() || b.find('.') != std::string::npos || b.find('/') != std::string::npos) {
    return {};
  }
  return b;
}

void add_edge(std::vector<WaveControlBosquejoEdge>* edges, const std::string& from,
              const std::string& to) {
  if (from.empty() || to.empty() || from == to) {
    return;
  }
  for (const auto& e : *edges) {
    if ((e.from == from && e.to == to) || (e.from == to && e.to == from)) {
      return;
    }
  }
  if (static_cast<int>(edges->size()) >= 6) {
    return;
  }
  edges->push_back({from, to});
}

void accumulate_hits(const std::vector<RegistryQueryHit>& hits, const std::string& q, bool scored,
                     float min_cos, const std::vector<std::vector<std::string>>& groups,
                     std::unordered_set<std::string>* seen, std::map<std::string, float>* mass,
                     std::map<std::string, std::string>* seed, int* n_hits,
                     std::ostringstream* dbg) {
  for (const auto& h : hits) {
    if (seen != nullptr && !h.node.id.empty() && !seen->insert(h.node.id).second) {
      continue;
    }
    const std::string b = barrio_of_hit(h.node);
    if (b.empty()) {
      continue;
    }
    float w = 0.f;
    if (scored) {
      w = symbol_anchor_score(h.node.symbol, q);
      if (w <= 0.f) {
        continue;
      }
      const int nmatch = groups.empty() ? 1 : std::max(1, groups_matched(h.node.symbol, groups));
      w *= static_cast<float>(nmatch);
    } else {
      if (h.cosine < min_cos) {
        continue;
      }
      w = 1.f;
    }
    (*mass)[b] += w;
    ++(*n_hits);
    if (seed->find(b) == seed->end()) {
      (*seed)[b] = h.node.id;
    }
    if (dbg != nullptr) {
      if (*n_hits <= 6) {
        *dbg << "      " << b << " " << h.node.symbol << " w=" << w << "\n";
      }
    }
  }
}

void pick_owner(const std::map<std::string, float>& mass, WaveControlOlor* olor) {
  float total = 0.f;
  std::string best;
  float best_n = 0.f;
  std::string second;
  float second_n = 0.f;
  for (const auto& kv : mass) {
    total += kv.second;
    if (kv.second > best_n) {
      second = best;
      second_n = best_n;
      best = kv.first;
      best_n = kv.second;
    } else if (kv.second > second_n) {
      second = kv.first;
      second_n = kv.second;
    }
  }
  if (total > 0.f && !best.empty()) {
    olor->barrio = best;
    olor->concentration = best_n / total;
    if (second_n * 4.f >= total && !second.empty()) {
      olor->twin = true;
      olor->twin_barrio = second;
    }
  }
}

}  // namespace

bool wave_control_bosquejo_from_registry(EffectRegistry* r, const RegistryEmbedFn& embed,
                                         const std::vector<std::string>& conceptos,
                                         WaveControlBosquejo* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    if (err) {
      *err = "bosquejo: registry";
    }
    return false;
  }
  std::string verr;
  if (!wave_control_bosquejar_ok(conceptos, &verr)) {
    if (err) {
      *err = verr.empty() ? "control: bosquejar 2–8 conceptos" : verr;
    }
    return false;
  }
  *out = {};
  std::ostringstream dbg;
  std::map<std::string, std::string> seed_by_barrio;
  std::unordered_set<std::string> owners;

  for (const auto& term : conceptos) {
    WaveControlOlor olor;
    olor.concepto = term;
    dbg << "  " << term << "\n";

    RegistryQueryOpts hop0;
    hop0.hops = 0;
    hop0.top_k = 24;
    hop0.match_surface = RegistryMatchSurface::NodeId;
    hop0.seed_kinds = {"fn", "latch"};
    std::string qerr;
    std::map<std::string, float> mass;
    const auto groups = phrase_groups(term);
    std::unordered_set<std::string> seen;
    for (const auto& q : concept_queries(term)) {
      RegistryQueryResult hop0_res;
      if (!registry_query(r, q, embed, hop0, &hop0_res, &qerr)) {
        continue;
      }
      accumulate_hits(hop0_res.hits, q, true, 0.f, groups, &seen, &mass, &seed_by_barrio,
                      &olor.hits, &dbg);
    }

    const bool used_embed =
        mass.empty() && static_cast<bool>(embed) &&
        (phrase_needs_embed(term) || !english_probe_phrase(term).empty());
    if (used_embed) {
      RegistryQueryOpts em;
      em.hops = 0;
      em.top_k = 8;
      em.match_surface = RegistryMatchSurface::CardFull;
      em.seed_kinds = {"fn", "latch"};
      std::vector<std::string> probes;
      probes.push_back(term);
      const std::string en = english_probe_phrase(term);
      if (!en.empty() && en != ascii_fold_term(term)) {
        probes.push_back(en);
      }
      float best = 0.f;
      std::vector<RegistryQueryHit> best_hits;
      std::string best_probe;
      for (const auto& p : probes) {
        RegistryQueryResult em_res;
        if (!registry_query(r, p, embed, em, &em_res, &qerr)) {
          continue;
        }
        for (const auto& h : em_res.hits) {
          if (h.cosine > best) {
            best = h.cosine;
            best_hits = em_res.hits;
            best_probe = p;
          }
        }
      }
      const float floor = ascii_fold_term(term).find(' ') != std::string::npos ? 0.62f : 0.55f;
      if (best >= floor) {
        dbg << "    embed \"" << best_probe << "\" best=" << best << " floor=" << floor << "\n";
        std::vector<RegistryQueryHit> keep;
        for (const auto& h : best_hits) {
          if (h.cosine + 0.03f >= best && h.cosine >= floor) {
            keep.push_back(h);
          }
        }
        accumulate_hits(keep, term, false, floor, groups, &seen, &mass, &seed_by_barrio, &olor.hits,
                        &dbg);
      }
    }

    pick_owner(mass, &olor);
    if (!olor.barrio.empty()) {
      owners.insert(olor.barrio);
    }
    dbg << "    → " << (olor.barrio.empty() ? "(sin olor)" : olor.barrio) << "\n";
    out->olores.push_back(std::move(olor));
  }

  for (const auto& b : owners) {
    const auto sit = seed_by_barrio.find(b);
    if (sit == seed_by_barrio.end() || sit->second.empty()) {
      continue;
    }
    std::vector<RegistryNeighbor> n1;
    std::string nerr;
    if (!registry_neighbors(r, sit->second, {}, "", &n1, &nerr)) {
      continue;
    }
    std::vector<std::string> hop1_ids;
    for (const auto& nb : n1) {
      const std::string to = barrio_of_hit(nb.node);
      if (owners.count(to) != 0) {
        add_edge(&out->entre, b, to);
      }
      if (!nb.node.id.empty()) {
        hop1_ids.push_back(nb.node.id);
      }
    }
    for (const auto& id : hop1_ids) {
      std::vector<RegistryNeighbor> n2;
      if (!registry_neighbors(r, id, {}, "", &n2, &nerr)) {
        continue;
      }
      for (const auto& nb : n2) {
        const std::string to = barrio_of_hit(nb.node);
        if (owners.count(to) != 0) {
          add_edge(&out->entre, b, to);
        }
      }
    }
  }

  if (owners.size() >= 2) {
    out->nota = "el mapa no une; un explorar cubre un barrio";
  } else if (owners.size() == 1) {
    out->nota = "un barrio concentra estos olores";
  } else {
    out->nota = "sin olor en el grafo";
  }
  out->detalle = dbg.str();
  return true;
}

}  // namespace tuide
