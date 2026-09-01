#include "ai/l2_wave.hpp"
#include "ai/action_json.hpp"
#include "ai/get_code_of.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace tuide {

bool outgoing_skip_name(const std::string& name);

namespace {

bool locus_keys_match(const std::string& a, const std::string& b);
void sketch_push(std::vector<WaveSketchLink>* edges, const std::string& from, const std::string& to,
                 const std::string& via);
std::string symbol_tail(const std::string& loc);
bool list_has_locus(const std::vector<std::string>& done, const std::string& loc,
                    const std::vector<WaveHit>& hits);

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string json_str(const nlohmann::json& j, const char* key) {
  if (!j.contains(key)) {
    return {};
  }
  if (j[key].is_string()) {
    return j[key].get<std::string>();
  }
  return {};
}

std::vector<std::string> json_str_array(const nlohmann::json& j, const char* key) {
  std::vector<std::string> out;
  if (!j.contains(key) || !j[key].is_array()) {
    return out;
  }
  for (const auto& item : j[key]) {
    if (item.is_string() && !item.get<std::string>().empty()) {
      out.push_back(item.get<std::string>());
    }
  }
  return out;
}

std::vector<std::string> json_str_or_array(const nlohmann::json& j, const char* key) {
  auto out = json_str_array(j, key);
  if (!out.empty()) {
    return out;
  }
  const std::string s = json_str(j, key);
  if (!s.empty()) {
    out.push_back(s);
  }
  return out;
}

void split_path_symbol(const std::string& target, std::string* path, std::string* symbol) {
  if (path == nullptr || symbol == nullptr) {
    return;
  }
  const auto slash = target.find_last_of("/\\");
  const auto colon = target.rfind(':');
  if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
    *path = target.substr(0, colon);
    *symbol = target.substr(colon + 1);
    return;
  }
  *path = target;
  *symbol = {};
}

bool looks_like_locator_line(const std::string& line) {
  if (line.empty() || line.find('`') != std::string::npos) {
    return false;
  }
  return line.find('/') != std::string::npos || line.find(".cpp") != std::string::npos ||
         line.find(".hpp") != std::string::npos || line.find(".h") != std::string::npos;
}

std::size_t utf8_prefix_end(const std::string& s, std::size_t n) {
  if (n >= s.size()) {
    return s.size();
  }
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
    --n;
  }
  return n;
}

void utf8_resize(std::string* s, std::size_t cap) {
  if (s == nullptr || s->size() <= cap) {
    return;
  }
  s->resize(utf8_prefix_end(*s, cap));
}

// Keep signature + closing region when the notebook cap is tighter than get_code_of.
std::string wave_clip_head_tail(std::string text, int cap) {
  if (cap <= 0 || static_cast<int>(text.size()) <= cap) {
    return text;
  }
  const std::string mark = "\n… [omitted mid] …\n";
  int keep = cap - static_cast<int>(mark.size());
  if (keep < 48) {
    utf8_resize(&text, static_cast<std::size_t>(std::max(0, cap)));
    text += "\n…\n";
    return text;
  }
  int head_n = std::max(24, (keep * 55) / 100);
  int tail_n = keep - head_n;
  if (tail_n < 24) {
    tail_n = 24;
    head_n = keep - tail_n;
  }
  std::string head = text.substr(0, utf8_prefix_end(text, static_cast<std::size_t>(head_n)));
  const auto hnl = head.rfind('\n');
  if (hnl != std::string::npos && hnl > 16) {
    head.resize(hnl + 1);
  }
  std::size_t tail_i = text.size() - static_cast<std::size_t>(tail_n);
  while (tail_i < text.size() && (static_cast<unsigned char>(text[tail_i]) & 0xC0) == 0x80) {
    ++tail_i;
  }
  std::string tail = text.substr(tail_i);
  const auto tnl = tail.find('\n');
  if (tnl != std::string::npos && tnl + 1 < tail.size()) {
    tail = tail.substr(tnl + 1);
  }
  return head + mark + tail;
}

std::string format_peek_note(const std::string& peek_id, const std::string& text) {
  std::ostringstream out;
  out << "### peek `" << peek_id << "`\n\n";
  if (text.find("```") != std::string::npos) {
    out << text;
    if (!text.empty() && text.back() != '\n') {
      out << '\n';
    }
    return out.str();
  }
  std::string path = peek_id;
  std::string body = text;
  const auto nl = text.find('\n');
  if (nl != std::string::npos) {
    const std::string head = text.substr(0, nl);
    if (looks_like_locator_line(head)) {
      path = head;
      body = text.substr(nl + 1);
      out << "`" << head << "`\n\n";
    }
  }
  out << wrap_source_fence(body, path);
  return out.str();
}

void add_unique_path(std::vector<std::string>* files, const std::string& path) {
  if (files == nullptr || path.empty()) {
    return;
  }
  const std::string want = ascii_lower(path);
  for (const auto& f : *files) {
    if (ascii_lower(f) == want) {
      return;
    }
  }
  files->push_back(path);
}

std::string sibling_pair_file(const std::string& path) {
  auto swap_ext = [&](const char* from, const char* to) -> std::string {
    const std::string lower = ascii_lower(path);
    const std::string f = ascii_lower(from);
    if (lower.size() < f.size() || lower.compare(lower.size() - f.size(), f.size(), f) != 0) {
      return {};
    }
    return path.substr(0, path.size() - f.size()) + to;
  };
  if (auto s = swap_ext(".cpp", ".hpp"); !s.empty()) {
    return s;
  }
  if (auto s = swap_ext(".cc", ".hpp"); !s.empty()) {
    return s;
  }
  if (auto s = swap_ext(".cxx", ".hpp"); !s.empty()) {
    return s;
  }
  if (auto s = swap_ext(".hpp", ".cpp"); !s.empty()) {
    return s;
  }
  if (auto s = swap_ext(".h", ".c"); !s.empty()) {
    return s;
  }
  if (auto s = swap_ext(".c", ".h"); !s.empty()) {
    return s;
  }
  return {};
}

void add_path_and_sibling(std::vector<std::string>* files, const std::string& path) {
  add_unique_path(files, path);
  const auto sib = sibling_pair_file(path);
  if (!sib.empty()) {
    add_unique_path(files, sib);
  }
}

bool looks_like_source_path(const std::string& s) {
  const std::string l = ascii_lower(s);
  return l.find(".cpp") != std::string::npos || l.find(".hpp") != std::string::npos ||
         l.find(".cc") != std::string::npos || l.find(".cxx") != std::string::npos ||
         (l.size() > 2 && l.compare(l.size() - 2, 2, ".h") == 0);
}

void push_unique_str(std::vector<std::string>* v, const std::string& s) {
  if (v == nullptr || s.empty()) {
    return;
  }
  const std::string want = ascii_lower(s);
  for (const auto& x : *v) {
    if (ascii_lower(x) == want) {
      return;
    }
  }
  v->push_back(s);
}

std::string join_list(const std::vector<std::string>& v, const char* sep = ", ") {
  std::ostringstream out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i != 0) {
      out << sep;
    }
    out << v[i];
  }
  return out.str();
}

std::string symbol_of_loc(const std::string& loc) {
  const auto col = loc.rfind("::");
  if (col != std::string::npos && loc.find('/') == std::string::npos) {
    return loc.substr(col + 2);
  }
  const auto slash = loc.find_last_of("/\\");
  const auto c = loc.rfind(':');
  if (c != std::string::npos && (slash == std::string::npos || c > slash)) {
    return loc.substr(c + 1);
  }
  return loc;
}

bool name_looks_on(const std::string& s) {
  const std::string l = ascii_lower(symbol_of_loc(s));
  return l.find("begin_") != std::string::npos || l.find("start_") != std::string::npos;
}

bool name_looks_off(const std::string& s) {
  const std::string l = ascii_lower(symbol_of_loc(s));
  return l.find("end_") != std::string::npos || l.find("stop_") != std::string::npos;
}

bool looks_like_field_needle(const std::string& needle) {
  if (needle.size() < 2 || needle.back() != '_') {
    return false;
  }
  return needle.find("::") == std::string::npos && needle.find('/') == std::string::npos;
}

std::string scrub_worker_leaks(const std::string& md) {
  std::istringstream in(md);
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("siguiente:") != std::string::npos ||
        line.find("{\"action\":") != std::string::npos ||
        line.find("causal_pilot_dataflow") != std::string::npos) {
      continue;
    }
    out << line << '\n';
  }
  return out.str();
}

std::string strip_mermaid_blocks(std::string s) {
  for (;;) {
    const auto p = s.find("```mermaid");
    if (p == std::string::npos) {
      break;
    }
    const auto q = s.find("```", p + 10);
    if (q == std::string::npos) {
      s.resize(p);
      break;
    }
    s.erase(p, q + 3 - p);
    while (p < s.size() && (s[p] == '\n' || s[p] == '\r')) {
      s.erase(p, 1);
    }
  }
  return s;
}

std::string marked_section_at(const std::string& md, std::size_t pos) {
  const auto next = md.find("\n----- ", pos + 1);
  if (next == std::string::npos) {
    return md.substr(pos);
  }
  return md.substr(pos, next - pos);
}

std::string collect_marked_sections(const std::string& md, const std::string& marker, int cap) {
  std::ostringstream out;
  std::size_t pos = 0;
  bool any = false;
  while ((pos = md.find(marker, pos)) != std::string::npos) {
    std::string block = strip_mermaid_blocks(marked_section_at(md, pos));
    block = wave_clip_head_tail(std::move(block), cap);
    if (block.empty()) {
      pos += marker.size();
      continue;
    }
    if (any) {
      out << "\n";
    }
    out << block;
    if (block.back() != '\n') {
      out << "\n";
    }
    any = true;
    pos += marker.size();
  }
  return out.str();
}

std::string collect_peek_sections(const std::string& notas) {
  std::ostringstream out;
  std::size_t pos = 0;
  bool any = false;
  while ((pos = notas.find("### peek `", pos)) != std::string::npos) {
    std::size_t end = notas.size();
    const auto next_peek = notas.find("\n### peek `", pos + 1);
    if (next_peek != std::string::npos) {
      end = next_peek;
    }
    auto cut_at = [&](const char* tok) {
      const auto p = notas.find(tok, pos);
      if (p != std::string::npos && p < end) {
        end = p;
      }
    };
    cut_at("\nfollow `");
    cut_at("\nin `");
    cut_at("\nentre `");
    std::string block = wave_clip_head_tail(notas.substr(pos, end - pos), kWaveWorkPeekChars);
    if (!block.empty()) {
      if (any) {
        out << "\n";
      }
      out << block;
      if (block.back() != '\n') {
        out << "\n";
      }
      any = true;
    }
    if (next_peek == std::string::npos) {
      break;
    }
    pos = next_peek + 1;
  }
  return out.str();
}

void emit_done_loci(std::ostringstream& out, const char* label, const std::vector<std::string>& done) {
  if (done.empty()) {
    return;
  }
  out << label;
  for (const auto& p : done) {
    out << " `" << p << "`";
  }
  out << "\n";
}

void note_circuit_from_peek(WaveState* st, const std::string& loc, const std::string& body,
                            const std::vector<WaveHit>& callers) {
  if (st == nullptr) {
    return;
  }
  const std::string key = loc;
  const bool on_name = name_looks_on(loc);
  const bool off_name = name_looks_off(loc);
  if (on_name) {
    push_unique_str(&st->circuit_on, key);
  }
  if (off_name) {
    push_unique_str(&st->circuit_off, key);
  }
  const auto calls = wave_extract_call_names(body);
  for (const auto& c : calls) {
    if (name_looks_on(c)) {
      push_unique_str(&st->circuit_on_via, c);
      if (!on_name && !off_name) {
        push_unique_str(&st->circuit_on, key);
      }
    }
    if (name_looks_off(c)) {
      push_unique_str(&st->circuit_off_via, c);
      if (!on_name && !off_name) {
        push_unique_str(&st->circuit_off, key);
      }
    }
  }
  auto* bucket = on_name ? &st->circuit_callers_on : (off_name ? &st->circuit_callers_off : nullptr);
  if (bucket != nullptr) {
    for (const auto& h : callers) {
      const std::string id = h.symbol.empty() ? wave_hit_key(h) : h.symbol;
      push_unique_str(bucket, id);
    }
  }
}

bool hop_in_keep_stems(const WaveState& st, const WaveHit& h) {
  std::vector<std::string> stems;
  for (const auto& z : st.zonas) {
    if (z.verdict != "keep") {
      continue;
    }
    if (const WaveHit* hit = wave_find_hit(st.candidatas, z.id)) {
      if (!hit->stem.empty()) {
        stems.push_back(ascii_lower(hit->stem));
      }
    }
  }
  if (stems.empty()) {
    return true;
  }
  const std::string hs = ascii_lower(h.stem);
  const std::string hp = ascii_lower(h.path);
  for (const auto& s : stems) {
    if ((!hs.empty() && hs == s) || (!hp.empty() && hp.find(s) != std::string::npos)) {
      return true;
    }
  }
  return false;
}

std::string neighbor_label(const WaveHit& h) {
  if (!h.path.empty() && !h.symbol.empty()) {
    return h.path + ":" + h.symbol;
  }
  if (!h.symbol.empty()) {
    return h.symbol;
  }
  return wave_hit_key(h);
}

void cap_unique_labels(std::vector<std::string>* dst, const std::string& label, int max_n) {
  if (dst == nullptr || label.empty()) {
    return;
  }
  push_unique_str(dst, label);
  if (static_cast<int>(dst->size()) > max_n) {
    dst->resize(static_cast<std::size_t>(max_n));
  }
}

bool neighbor_id_skip(const std::string& loc) {
  if (loc.size() < 2 || loc.size() > 4 || loc[0] != 'M') {
    return false;
  }
  for (std::size_t i = 1; i < loc.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(loc[i]))) {
      return false;
    }
  }
  return true;
}

bool neighbor_call_noise(const std::string& name) {
  if (outgoing_skip_name(name)) {
    return true;
  }
  const std::string low = ascii_lower(symbol_tail(name));
  if (low.find("i18n") != std::string::npos) {
    return true;
  }
  if (low.size() >= 3 && low.compare(low.size() - 3, 3, "_ms") == 0) {
    return true;
  }
  if (low.rfind("paint_", 0) == 0 || low.rfind("steady_", 0) == 0) {
    return true;
  }
  if (low == "tr") {
    return true;
  }
  return false;
}

int neighbor_caller_score(const WaveState& st, const std::string& loc) {
  int score = 0;
  const std::string tail = ascii_lower(symbol_tail(loc));
  if (name_looks_on(loc)) {
    score += 8;
  }
  if (name_looks_off(loc) && tail.find("download") == std::string::npos &&
      tail.find("embed") == std::string::npos) {
    score += 8;
  }
  if (tail.rfind("run_", 0) == 0 || tail.rfind("handle_", 0) == 0) {
    score += 6;
  }
  const std::string q = ascii_lower(st.prompt);
  const bool spinner_q = q.find("spinner") != std::string::npos || q.find("chat") != std::string::npos ||
                         q.find("carga") != std::string::npos || q.find("modelo") != std::string::npos;
  if (spinner_q && (tail.find("think") != std::string::npos || tail.find("busy") != std::string::npos ||
                    tail.find("spinner") != std::string::npos)) {
    score += 10;
  }
  if (tail.find("download") != std::string::npos || tail.find("embed") != std::string::npos) {
    if (q.find("descarg") == std::string::npos && q.find("download") == std::string::npos &&
        q.find("embed") == std::string::npos) {
      score -= 4;
    }
  }
  return score;
}

bool neighbor_is_read(const WaveState& st, const std::string& loc) {
  return list_has_locus(st.peeks_done, loc, st.candidatas) ||
         list_has_locus(st.follows_done, loc, st.candidatas);
}

std::string format_peek_neighbors(const WaveState& st, const WavePeekNeighbors& n) {
  if (n.callers.empty() && n.callees.empty()) {
    return {};
  }
  std::ostringstream out;
  if (!n.callers.empty()) {
    out << "callers:";
    for (const auto& c : n.callers) {
      out << " `" << c << "`";
      if (!neighbor_is_read(st, c)) {
        out << " (no leído)";
      }
    }
    out << "\n";
  }
  if (!n.callees.empty()) {
    out << "calls:";
    for (const auto& c : n.callees) {
      out << " `" << c << "`";
    }
    out << "\n";
  }
  return out.str();
}

void record_peek_neighbors(WaveState* st, const std::string& loc, const std::string& body,
                           const std::vector<WaveHit>& callers) {
  if (st == nullptr || loc.empty()) {
    return;
  }
  WavePeekNeighbors n;
  n.loc = loc;
  std::vector<std::pair<int, std::string>> ranked;
  for (const auto& h : callers) {
    if (!hop_in_keep_stems(*st, h)) {
      continue;
    }
    const std::string label = neighbor_label(h);
    if (label.empty() || neighbor_id_skip(label)) {
      continue;
    }
    bool dup = false;
    for (const auto& have : ranked) {
      if (locus_keys_match(have.second, label)) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      ranked.push_back({neighbor_caller_score(*st, label), label});
    }
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second < b.second;
  });
  for (const auto& row : ranked) {
    n.callers.push_back(row.second);
    if (static_cast<int>(n.callers.size()) >= kWavePeekNeighborMax) {
      break;
    }
  }
  for (const auto& c : wave_follow_outgoing_calls(body)) {
    if (c.symbol.empty() || locus_keys_match(c.symbol, loc) || neighbor_call_noise(c.symbol)) {
      continue;
    }
    cap_unique_labels(&n.callees, c.symbol, kWavePeekNeighborMax);
    if (static_cast<int>(n.callees.size()) >= kWavePeekNeighborMax) {
      break;
    }
  }
  for (auto& have : st->peek_neighbors) {
    if (locus_keys_match(have.loc, loc)) {
      have = std::move(n);
      return;
    }
  }
  st->peek_neighbors.push_back(std::move(n));
}

void sketch_push(std::vector<WaveSketchLink>* edges, const std::string& from, const std::string& to,
                 const std::string& via) {
  if (edges == nullptr || from.empty() || to.empty() || locus_keys_match(from, to)) {
    return;
  }
  for (const auto& e : *edges) {
    if (locus_keys_match(e.from, from) && locus_keys_match(e.to, to) && e.via == via) {
      return;
    }
  }
  if (static_cast<int>(edges->size()) >= kWaveSketchEdgesMax) {
    return;
  }
  edges->push_back({from, to, via});
}

std::string strip_fn_prefix(std::string t) {
  if (t.size() > 3 && ascii_lower(t.substr(0, 3)) == "fn:") {
    return t.substr(3);
  }
  return t;
}

bool fill_hit_from_locator(WaveHit* h, const std::string& loc, const std::string& stem_fb) {
  if (h == nullptr || loc.empty()) {
    return false;
  }
  const std::string t = strip_fn_prefix(loc);
  if (t.find('/') != std::string::npos || looks_like_source_path(t)) {
    split_path_symbol(t, &h->path, &h->symbol);
  } else {
    const auto col = t.rfind("::");
    if (col != std::string::npos && col > 0 && col + 2 < t.size()) {
      if (h->stem.empty()) {
        h->stem = t.substr(0, col);
      }
      if (h->symbol.empty()) {
        h->symbol = t.substr(col + 2);
      }
    } else if (h->symbol.empty()) {
      h->symbol = t;
    }
  }
  if (h->stem.empty()) {
    h->stem = stem_fb;
  }
  if (!h->path.empty()) {
    add_path_and_sibling(&h->files, h->path);
  }
  return !h->symbol.empty() || !h->path.empty();
}

void harvest_target_files(std::vector<std::string>* files, const std::string& target) {
  if (target.empty()) {
    return;
  }
  std::string path;
  std::string symbol;
  split_path_symbol(target, &path, &symbol);
  if (!path.empty() && looks_like_source_path(path)) {
    add_path_and_sibling(files, path);
  }
}

void collect_zone_files(const nlohmann::json& zone, std::vector<std::string>* files) {
  auto walk_obj = [&](const nlohmann::json& o) {
    if (!o.is_object()) {
      return;
    }
    harvest_target_files(files, json_str(o, "target"));
    harvest_target_files(files, json_str(o, "from"));
    harvest_target_files(files, json_str(o, "to"));
  };
  const auto walk_arr = [&](const char* key) {
    if (!zone.contains(key) || !zone[key].is_array()) {
      return;
    }
    for (const auto& item : zone[key]) {
      if (item.is_array()) {
        for (const auto& inner : item) {
          walk_obj(inner);
        }
      } else {
        walk_obj(item);
      }
    }
  };
  walk_arr("anchors");
  walk_arr("representatives");
  walk_arr("ports");
  walk_arr("edges");
  if (zone.contains("mechanism") && zone["mechanism"].is_object()) {
    for (const auto& kv : zone["mechanism"].items()) {
      walk_obj(kv.value());
    }
  }
}

void merge_files_into(WaveHit* dst, const std::vector<std::string>& extra) {
  if (dst == nullptr) {
    return;
  }
  for (const auto& f : extra) {
    add_unique_path(&dst->files, f);
  }
  if (static_cast<int>(dst->files.size()) > 6) {
    dst->files.resize(6);
  }
}

std::string symbol_tail(const std::string& loc) {
  const auto slash = loc.find_last_of("/\\");
  const auto qual = loc.rfind("::");
  if (qual != std::string::npos && (slash == std::string::npos || qual > slash)) {
    return ascii_lower(loc.substr(qual + 2));
  }
  const auto colon = loc.rfind(':');
  if (colon != std::string::npos && (slash == std::string::npos || colon > slash) &&
      colon + 1 < loc.size()) {
    return ascii_lower(loc.substr(colon + 1));
  }
  return ascii_lower(loc);
}

bool locus_keys_match(const std::string& a, const std::string& b) {
  const std::string la = ascii_lower(a);
  const std::string lb = ascii_lower(b);
  if (la == lb) {
    return true;
  }
  const std::string ta = symbol_tail(a);
  const std::string tb = symbol_tail(b);
  return !ta.empty() && ta == tb && ta.size() >= 4;
}

bool needle_already_logged(const WaveState& st, const std::string& needle,
                           const std::string& in_locus = {}) {
  for (const auto& rec : st.needles_log) {
    if (!locus_keys_match(rec.needle, needle)) {
      continue;
    }
    if (in_locus.empty() && rec.in_locus.empty()) {
      return true;
    }
    if (!in_locus.empty() && !rec.in_locus.empty() &&
        locus_keys_match(rec.in_locus, in_locus)) {
      return true;
    }
  }
  return false;
}

bool is_file_only_loc(const std::string& loc) {
  return looks_like_source_path(loc) && loc.find(':') == std::string::npos &&
         loc.find("::") == std::string::npos;
}

bool list_has_locus(const std::vector<std::string>& done, const std::string& loc,
                    const std::vector<WaveHit>& hits) {
  if (is_file_only_loc(loc)) {
    const std::string want = ascii_lower(loc);
    for (const auto& d : done) {
      if (ascii_lower(d) == want) {
        return true;
      }
    }
    return false;
  }
  for (const auto& d : done) {
    if (is_file_only_loc(d)) {
      continue;
    }
    if (locus_keys_match(d, loc)) {
      return true;
    }
  }
  if (const WaveHit* hit = wave_find_hit(hits, loc)) {
    const std::string key = wave_hit_key(*hit);
    for (const auto& d : done) {
      if (is_file_only_loc(d)) {
        continue;
      }
      if (locus_keys_match(d, key) || locus_keys_match(d, hit->symbol) ||
          locus_keys_match(d, hit->id)) {
        return true;
      }
    }
  }
  return false;
}

void mark_locus(std::vector<std::string>* done, const std::string& loc, const WaveState& st) {
  if (done == nullptr || loc.empty()) {
    return;
  }
  auto push = [&](const std::string& s) {
    if (s.empty()) {
      return;
    }
    for (const auto& d : *done) {
      if (ascii_lower(d) == ascii_lower(s)) {
        return;
      }
    }
    done->push_back(s);
  };
  if (is_file_only_loc(loc)) {
    push(loc);
    return;
  }
  push(loc);
  push(symbol_tail(loc));
  if (const WaveHit* hit = wave_find_hit(st.candidatas, loc)) {
    push(wave_hit_key(*hit));
    push(hit->symbol);
    if (!hit->stem.empty() && !hit->symbol.empty()) {
      push(hit->stem + "::" + hit->symbol);
    }
  }
}

bool peek_is_allowed(const WaveState& st, const std::string& peek) {
  if (peek.size() < 2) {
    return false;
  }
  if (wave_find_hit(st.candidatas, peek) != nullptr) {
    return true;
  }
  for (const auto& z : st.zonas) {
    if (z.verdict == "keep" && ascii_lower(z.id) == ascii_lower(peek)) {
      return true;
    }
  }
  const std::string want = ascii_lower(peek);
  for (const auto& m : st.mencionados) {
    if (ascii_lower(m) == want) {
      return true;
    }
  }
  const auto col = peek.rfind("::");
  if (col != std::string::npos && col > 0 && col + 2 < peek.size()) {
    return true;
  }
  if (looks_like_source_path(peek)) {
    return true;
  }
  return false;
}

void collect_peeks(WaveOla* ola) {
  if (ola == nullptr) {
    return;
  }
  if (ola->peeks.empty() && !ola->peek.empty()) {
    ola->peeks.push_back(ola->peek);
  }
  if (ola->peek.empty() && !ola->peeks.empty()) {
    ola->peek = ola->peeks.front();
  }
  if (static_cast<int>(ola->peeks.size()) > kWaveMaxPeeks) {
    ola->peeks.resize(static_cast<size_t>(kWaveMaxPeeks));
  }
}

void collect_follows(WaveOla* ola) {
  if (ola == nullptr) {
    return;
  }
  if (static_cast<int>(ola->follows.size()) > kWaveMaxFollows) {
    ola->follows.resize(static_cast<size_t>(kWaveMaxFollows));
  }
}

void locate_peek(WaveState* st, const std::string& peek, const WaveOps& ops);
void resolve_locus(const WaveState& st, const std::string& loc, std::string* path,
                   std::string* symbol);
void append_follow_md(WaveState* st, std::string md, int cap);
bool loc_resolves_fn(const WaveState& st, const std::string& loc, std::string* path,
                     std::string* symbol);

void append_mentions(WaveState* st, const std::string& text, std::string* listed) {
  if (st == nullptr) {
    return;
  }
  const auto names = wave_extract_call_names(text);
  std::unordered_set<std::string> have;
  for (const auto& m : st->mencionados) {
    have.insert(ascii_lower(m));
  }
  std::ostringstream mentioned;
  for (const auto& n : names) {
    const std::string key = ascii_lower(n);
    if (!have.insert(key).second) {
      continue;
    }
    if (static_cast<int>(st->mencionados.size()) >= kWaveMaxMentions) {
      break;
    }
    st->mencionados.push_back(n);
    if (!mentioned.str().empty()) {
      mentioned << ", ";
    }
    mentioned << n;
  }
  if (listed != nullptr) {
    *listed = mentioned.str();
  }
}

bool apply_needles(WaveState* st, const std::vector<std::string>& needles, const WaveOps& ops,
                   std::string* detail, std::string* err, bool log = true,
                   const std::string& in_locus = {}) {
  if (!in_locus.empty()) {
    if (!ops.search_in_body) {
      if (err) {
        *err = "sin grep acotado";
      }
      return false;
    }
    std::vector<std::string> fresh;
    for (const auto& needle : needles) {
      if (!needle_already_logged(*st, needle, in_locus)) {
        fresh.push_back(needle);
      }
    }
    if (fresh.empty()) {
      if (detail != nullptr) {
        *detail = in_locus + " (ya grep en este cuerpo)";
      }
      return true;
    }
    locate_peek(st, in_locus, ops);
    std::string path;
    std::string symbol;
    if (!loc_resolves_fn(*st, in_locus, &path, &symbol)) {
      if (err) {
        *err = "in exige símbolo (path:fn), no un archivo";
      }
      return false;
    }
    std::string md;
    std::vector<int> hits;
    std::string ierr;
    if (!ops.search_in_body(path, symbol, fresh, &md, &hits, &ierr) || md.empty()) {
      if (err) {
        *err = ierr.empty() ? "in falló" : ierr;
      }
      return false;
    }
    if (!st->notas.empty() && st->notas.back() != '\n') {
      st->notas += "\n";
    }
    st->notas += "in `" + in_locus + "` → grep en el cuerpo\n";
    append_follow_md(st, md, kWaveInBodyChars);
    std::ostringstream det;
    for (std::size_t i = 0; i < fresh.size(); ++i) {
      WaveNeedleLog rec;
      rec.needle = fresh[i];
      rec.in_locus = in_locus;
      rec.hits = i < hits.size() ? hits[i] : 0;
      rec.added = 0;
      if (log) {
        if (!det.str().empty()) {
          det << "; ";
        }
        det << rec.needle << "@" << symbol << " hits=" << rec.hits;
        st->needles_log.push_back(std::move(rec));
      }
    }
    if (detail != nullptr) {
      *detail = det.str();
    }
    return true;
  }
  if (!ops.search_needle) {
    if (err) {
      *err = "sin buscador de needles";
    }
    return false;
  }
  std::ostringstream det;
  for (const auto& needle : needles) {
    if (needle_already_logged(*st, needle, {})) {
      continue;
    }
    WaveNeedleLog rec;
    rec.needle = needle;
    const std::string hint = wave_needle_stem_hint(needle);
    std::unordered_set<std::string> seen;
    std::vector<WaveHit> batch;
    for (const auto& key : wave_needle_search_keys(needle)) {
      auto hits = ops.search_needle(key, hint.empty() ? st->campo : hint);
      for (auto& h : hits) {
        if (h.needle.empty()) {
          h.needle = needle;
        }
        if (!hint.empty() && !wave_campo_match(h, hint)) {
          continue;
        }
        if (!h.path.empty()) {
          add_path_and_sibling(&h.files, h.path);
        }
        const std::string k = ascii_lower(wave_hit_key(h));
        if (k.empty() || !seen.insert(k).second) {
          continue;
        }
        batch.push_back(std::move(h));
      }
    }
    rec.hits = static_cast<int>(batch.size());
    for (const auto& h : batch) {
      if (static_cast<int>(rec.ids.size()) >= 6) {
        break;
      }
      rec.ids.push_back(wave_hit_key(h));
    }
    const auto n0 = st->candidatas.size();
    wave_merge_hits(st, batch);
    rec.added = static_cast<int>(st->candidatas.size() - n0);
    if (log) {
      if (!det.str().empty()) {
        det << "; ";
      }
      det << rec.needle << " hits=" << rec.hits << " +" << rec.added;
      st->needles_log.push_back(std::move(rec));
    }
  }
  if (detail != nullptr) {
    *detail = det.str();
  }
  return true;
}

void locate_peek(WaveState* st, const std::string& peek, const WaveOps& ops) {
  if (st == nullptr || peek.size() < 2) {
    return;
  }
  if (wave_find_hit(st->candidatas, peek) != nullptr) {
    return;
  }
  if (!ops.search_needle) {
    return;
  }
  std::string ignored;
  apply_needles(st, {peek}, ops, &ignored, nullptr, false);
}

void resolve_locus(const WaveState& st, const std::string& loc, std::string* path,
                   std::string* symbol) {
  if (path == nullptr || symbol == nullptr) {
    return;
  }
  path->clear();
  symbol->clear();
  if (const WaveHit* hit = wave_find_hit(st.candidatas, loc)) {
    *path = hit->path;
    *symbol = hit->symbol;
  }
  if (symbol->empty()) {
    const auto col = loc.rfind("::");
    if (col != std::string::npos && loc.find('/') == std::string::npos) {
      *symbol = loc.substr(col + 2);
    } else {
      split_path_symbol(loc, path, symbol);
    }
  }
}

bool loc_resolves_fn(const WaveState& st, const std::string& loc, std::string* path,
                     std::string* symbol) {
  if (is_file_only_loc(loc)) {
    return false;
  }
  std::string p;
  std::string s;
  resolve_locus(st, loc, &p, &s);
  if (p.empty() || s.empty()) {
    return false;
  }
  if (path != nullptr) {
    *path = p;
  }
  if (symbol != nullptr) {
    *symbol = s;
  }
  return true;
}

bool loc_is_stem_campo(const std::string& loc, std::string* campo) {
  const auto col = loc.rfind("::");
  if (col == std::string::npos || loc.find('/') != std::string::npos) {
    return false;
  }
  if (ascii_lower(loc.substr(0, col)) != "stem") {
    return false;
  }
  const std::string rest = loc.substr(col + 2);
  if (rest.empty()) {
    return false;
  }
  if (campo != nullptr) {
    *campo = rest;
  }
  return true;
}

// A bad `in` in a tanda must not drop peeks/follows: clear it. Do not rewrite
// `stem::módulo` into campo — that key is only for the JSON field `campo`.
void normalize_ola_in(WaveOla* ola, const WaveState& st) {
  if (ola == nullptr || ola->in_locus.empty()) {
    return;
  }
  if (loc_resolves_fn(st, ola->in_locus, nullptr, nullptr)) {
    return;
  }
  if (ola->do_kind == WaveDo::Tanda) {
    ola->in_locus.clear();
  }
}

std::string peek_code_arg(const WaveState& st, const std::string& peek) {
  if (is_file_only_loc(peek)) {
    return peek;
  }
  std::string path;
  std::string symbol;
  if (loc_resolves_fn(st, peek, &path, &symbol)) {
    return path + ":" + symbol;
  }
  return peek;
}

void merge_follow_hops(WaveState* st, std::vector<WaveHit> hops) {
  for (auto& c : hops) {
    if (c.needle.empty()) {
      c.needle = "follow";
    }
    if (c.id.empty()) {
      c.id = c.path.empty() ? c.symbol : (c.path + ":" + c.symbol);
    }
    if (!c.path.empty()) {
      add_path_and_sibling(&c.files, c.path);
    }
  }
  wave_merge_hits(st, hops);
}

void append_follow_md(WaveState* st, std::string md, int cap) {
  if (st == nullptr || md.empty()) {
    return;
  }
  md = scrub_worker_leaks(md);
  if (md.empty()) {
    return;
  }
  if (static_cast<int>(md.size()) > cap) {
    utf8_resize(&md, static_cast<std::size_t>(cap));
    md += "\n…\n";
  }
  if (!st->follow_md.empty() && st->follow_md.back() != '\n') {
    st->follow_md += "\n";
  }
  st->follow_md += md;
}

bool apply_peeks(WaveState* st, const std::vector<std::string>& peeks, const WaveOps& ops,
                 bool skip_fail, int* ok_n, std::string* detail, std::string* err) {
  if (!ops.peek_code) {
    if (err) {
      *err = "sin peek de código";
    }
    return false;
  }
  std::ostringstream det;
  int n = 0;
  for (const auto& peek : peeks) {
    if (list_has_locus(st->peeks_done, peek, st->candidatas)) {
      if (!det.str().empty()) {
        det << "; ";
      }
      det << peek << " (ya leído)";
      continue;
    }
    locate_peek(st, peek, ops);
    std::string text;
    std::string peek_err;
    const std::string arg = peek_code_arg(*st, peek);
    if (!ops.peek_code(arg, &text, &peek_err)) {
      if (!skip_fail) {
        if (err) {
          *err = peek_err.empty() ? "peek falló" : peek_err;
        }
        return false;
      }
      if (!det.str().empty()) {
        det << "; ";
      }
      det << peek << " (sin cuerpo)";
      continue;
    }
    const std::string body = text;
    const bool gco_trunc = body.find("[omitted lines") != std::string::npos ||
                           body.find("[TRUNCATED]") != std::string::npos;
    const bool clipped = static_cast<int>(body.size()) > kWavePeekChars;
    const bool truncated = gco_trunc || clipped;
    text = wave_clip_head_tail(body, kWavePeekChars);
    if (!st->notas.empty()) {
      st->notas += "\n";
    }
    st->notas += format_peek_note(peek, text);
    std::vector<WaveHit> callers;
    const bool file_only = looks_like_source_path(peek) && peek.find(':') == std::string::npos &&
                           peek.find("::") == std::string::npos;
    if (!file_only && ops.peek_causal) {
      std::string path;
      std::string symbol;
      resolve_locus(*st, peek, &path, &symbol);
      if (!symbol.empty()) {
        std::string md;
        std::string cerr;
        if (ops.peek_causal(path, symbol, body, truncated, &md, &callers, &cerr) && !md.empty()) {
          if (static_cast<int>(md.size()) > kWaveFollowChars) {
            utf8_resize(&md, static_cast<std::size_t>(kWaveFollowChars));
            md += "\n…\n";
          }
          append_follow_md(st, md, kWaveFollowChars);
          std::vector<WaveHit> kept;
          for (const auto& h : callers) {
            if (hop_in_keep_stems(*st, h)) {
              kept.push_back(h);
            }
          }
          if (!kept.empty()) {
            callers = std::move(kept);
          }
          merge_follow_hops(st, callers);
        }
      }
    }
    std::string path;
    std::string symbol;
    resolve_locus(*st, peek, &path, &symbol);
    const std::string circuit_loc =
        (!path.empty() && !symbol.empty()) ? (path + ":" + symbol) : peek;
    if (!file_only) {
      note_circuit_from_peek(st, circuit_loc, body, callers);
      record_peek_neighbors(st, circuit_loc, body, callers);
      for (const auto& n : st->peek_neighbors) {
        if (!locus_keys_match(n.loc, circuit_loc)) {
          continue;
        }
        const std::string neigh = format_peek_neighbors(*st, n);
        if (!neigh.empty()) {
          if (!st->notas.empty() && st->notas.back() != '\n') {
            st->notas += "\n";
          }
          st->notas += neigh;
        }
        break;
      }
    }
    std::string mentioned;
    append_mentions(st, body, &mentioned);
    if (!det.str().empty()) {
      det << "; ";
    }
    det << peek;
    if (!mentioned.empty()) {
      det << " → " << mentioned;
    }
    mark_locus(&st->peeks_done, peek, *st);
    ++n;
  }
  if (ok_n != nullptr) {
    *ok_n = n;
  }
  if (detail != nullptr) {
    *detail = det.str();
  }
  return n > 0 || skip_fail;
}

bool apply_follows(WaveState* st, const std::vector<std::string>& follows, const WaveOps& ops,
                   bool skip_fail, int* ok_n, std::string* detail, std::string* err) {
  if (!ops.follow_tree) {
    if (err) {
      *err = "sin follow causal";
    }
    return false;
  }
  std::ostringstream det;
  int n = 0;
  for (const auto& loc : follows) {
    if (list_has_locus(st->follows_done, loc, st->candidatas)) {
      if (!det.str().empty()) {
        det << "; ";
      }
      det << loc << " (ya seguido)";
      continue;
    }
    locate_peek(st, loc, ops);
    std::string path;
    std::string symbol;
    resolve_locus(*st, loc, &path, &symbol);
    if (symbol.empty()) {
      if (!skip_fail) {
        if (err) {
          *err = "follow sin símbolo";
        }
        return false;
      }
      if (!det.str().empty()) {
        det << "; ";
      }
      det << loc << " (sin símbolo)";
      continue;
    }
    std::string md;
    std::vector<WaveHit> hops;
    std::string ferr;
    if (!ops.follow_tree(path, symbol, &md, &hops, &ferr) || md.empty()) {
      if (!skip_fail) {
        if (err) {
          *err = ferr.empty() ? "follow falló" : ferr;
        }
        return false;
      }
      if (!det.str().empty()) {
        det << "; ";
      }
      det << loc << " (sin causal)";
      continue;
    }
    append_follow_md(st, md, kWaveFollowTreeChars);
    if (!st->notas.empty() && st->notas.back() != '\n') {
      st->notas += "\n";
    }
    st->notas += "follow `" + loc + "` → callers + callees (cond/mermaid)\n";
    const std::string circuit_loc =
        (!path.empty() && !symbol.empty()) ? (path + ":" + symbol) : loc;
    for (const auto& h : hops) {
      const std::string hop = neighbor_label(h);
      if (hop.empty() || locus_keys_match(hop, circuit_loc)) {
        continue;
      }
      bool dup = false;
      for (const auto& l : st->follow_links) {
        if (locus_keys_match(l.from, hop) && locus_keys_match(l.to, circuit_loc)) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        st->follow_links.push_back({hop, circuit_loc, "follow"});
      }
    }
    merge_follow_hops(st, std::move(hops));
    if (name_looks_on(circuit_loc)) {
      push_unique_str(&st->circuit_on, circuit_loc);
    }
    if (name_looks_off(circuit_loc)) {
      push_unique_str(&st->circuit_off, circuit_loc);
    }
    if (!det.str().empty()) {
      det << "; ";
    }
    det << loc;
    mark_locus(&st->follows_done, loc, *st);
    ++n;
  }
  if (ok_n != nullptr) {
    *ok_n = n;
  }
  if (detail != nullptr) {
    *detail = det.str();
  }
  return n > 0 || skip_fail;
}

std::string entre_canon_end(const WaveState& st, const std::string& loc) {
  if (const WaveHit* hit = wave_find_hit(st.candidatas, loc)) {
    if (!hit->symbol.empty()) {
      return ascii_lower(hit->symbol);
    }
    return ascii_lower(wave_hit_key(*hit));
  }
  return symbol_tail(loc);
}

std::string entre_pair_key(const WaveState& st, const std::string& from, const std::string& to) {
  return entre_canon_end(st, from) + "\t" + entre_canon_end(st, to);
}

bool entre_already_done(const WaveState& st, const std::string& from, const std::string& to) {
  const std::string key = entre_pair_key(st, from, to);
  for (const auto& d : st.entres_done) {
    if (d == key) {
      return true;
    }
  }
  return false;
}

bool apply_entre(WaveState* st, const std::string& from, const std::string& to, const WaveOps& ops,
                 bool skip_fail, int* ok_n, std::string* detail, std::string* err) {
  if (!ops.path_between) {
    if (err) {
      *err = "sin camino entre";
    }
    return false;
  }
  if (from.empty() || to.empty()) {
    if (err) {
      *err = "entre sin from/to";
    }
    return false;
  }
  std::ostringstream det;
  if (entre_already_done(*st, from, to)) {
    det << from << " → " << to << " (ya pedido)";
    if (ok_n != nullptr) {
      *ok_n = 0;
    }
    if (detail != nullptr) {
      *detail = det.str();
    }
    return skip_fail;
  }
  locate_peek(st, from, ops);
  locate_peek(st, to, ops);
  std::string md;
  std::vector<WaveHit> hops;
  std::string perr;
  if (!ops.path_between(from, to, &md, &hops, &perr) || md.empty()) {
    if (!skip_fail) {
      if (err) {
        *err = perr.empty() ? "entre falló" : perr;
      }
      return false;
    }
    det << from << " → " << to << " (sin camino)";
    if (ok_n != nullptr) {
      *ok_n = 0;
    }
    if (detail != nullptr) {
      *detail = det.str();
    }
    return true;
  }
  append_follow_md(st, md, kWaveFollowTreeChars);
  if (!st->notas.empty() && st->notas.back() != '\n') {
    st->notas += "\n";
  }
  st->notas += "entre `" + from + "` → `" + to + "`\n";
  for (auto& h : hops) {
    if (h.needle.empty()) {
      h.needle = "entre";
    }
  }
  merge_follow_hops(st, std::move(hops));
  st->entres_done.push_back(entre_pair_key(*st, from, to));
  det << from << " → " << to;
  if (ok_n != nullptr) {
    *ok_n = 1;
  }
  if (detail != nullptr) {
    *detail = det.str();
  }
  return true;
}

void maybe_auto_entre(WaveState* st, const WaveOps& ops) {
  if (st == nullptr || st->circuit_on.empty() || st->circuit_off.empty()) {
    return;
  }
  if (!st->circuit_entre.empty() || !ops.path_between) {
    return;
  }
  const std::string& from = st->circuit_on.front();
  const std::string& to = st->circuit_off.front();
  if (entre_already_done(*st, from, to)) {
    st->circuit_entre = from + " → " + to;
    return;
  }
  std::string detail;
  std::string err;
  int ok = 0;
  if (apply_entre(st, from, to, ops, true, &ok, &detail, &err)) {
    st->circuit_entre = detail.empty() ? (from + " → " + to) : detail;
  } else {
    st->circuit_entre = err.empty() ? std::string("sin camino") : err;
  }
}

void push_ola_log(WaveState* st, const char* do_name, const std::string& why,
                  const std::string& detail) {
  WaveOlaLog e;
  e.n = st->wave_n + 1;
  e.do_name = do_name;
  e.why = why;
  e.detail = detail;
  st->olas_log.push_back(std::move(e));
  ++st->wave_n;
}

}  // namespace

bool wave_campo_match(const WaveHit& hit, const std::string& campo) {
  if (campo.empty()) {
    return true;
  }
  const std::string c = ascii_lower(campo);
  const std::string stem = ascii_lower(hit.stem);
  const std::string path = ascii_lower(hit.path);
  const std::string id = ascii_lower(hit.id);
  if (!stem.empty() && stem == c) {
    return true;
  }
  if (path.find(c) != std::string::npos) {
    return true;
  }
  if (id.find(c) != std::string::npos) {
    return true;
  }
  return false;
}

std::string wave_hit_key(const WaveHit& hit) {
  if (!hit.id.empty()) {
    return hit.id;
  }
  if (!hit.path.empty() && !hit.symbol.empty()) {
    return hit.path + ":" + hit.symbol;
  }
  return hit.symbol;
}

bool wave_id_in_hits(const std::vector<WaveHit>& hits, const std::string& id) {
  return wave_find_hit(hits, id) != nullptr;
}

const WaveHit* wave_find_hit(const std::vector<WaveHit>& hits, const std::string& id) {
  if (id.empty()) {
    return nullptr;
  }
  const std::string needle = ascii_lower(id);
  const WaveHit* prefix = nullptr;
  std::string want_stem;
  std::string want_sym;
  const auto col = id.rfind("::");
  if (col != std::string::npos && col > 0 && col + 2 < id.size()) {
    want_stem = ascii_lower(id.substr(0, col));
    want_sym = ascii_lower(id.substr(col + 2));
  }
  for (const auto& h : hits) {
    if (ascii_lower(h.id) == needle) {
      return &h;
    }
    if (!h.path.empty() && !h.symbol.empty() &&
        ascii_lower(h.path + ":" + h.symbol) == needle) {
      return &h;
    }
    if (!h.symbol.empty() && ascii_lower(h.symbol) == needle) {
      return &h;
    }
    if (!h.path.empty() && ascii_lower(h.path) == needle) {
      return &h;
    }
    if (!h.stem.empty() && !h.symbol.empty() &&
        ascii_lower(h.stem + "::" + h.symbol) == needle) {
      return &h;
    }
    for (const auto& f : h.files) {
      if (ascii_lower(f) == needle) {
        return &h;
      }
    }
    if (!want_stem.empty() && ascii_lower(h.stem) == want_stem) {
      const std::string hs = ascii_lower(h.symbol);
      if (hs == want_sym) {
        return &h;
      }
      if (!want_sym.empty() && hs.size() >= want_sym.size() &&
          hs.compare(0, want_sym.size(), want_sym) == 0) {
        if (prefix == nullptr) {
          prefix = &h;
        }
      }
    }
  }
  return prefix;
}

void wave_merge_hits(WaveState* st, const std::vector<WaveHit>& incoming) {
  if (st == nullptr) {
    return;
  }
  std::unordered_set<std::string> seen;
  for (const auto& h : st->candidatas) {
    seen.insert(ascii_lower(wave_hit_key(h)));
  }
  for (const auto& h : incoming) {
    if (!wave_campo_match(h, st->campo)) {
      continue;
    }
    const std::string key = ascii_lower(wave_hit_key(h));
    if (key.empty()) {
      continue;
    }
    bool merged = false;
    for (auto& have : st->candidatas) {
      if (ascii_lower(wave_hit_key(have)) != key) {
        continue;
      }
      merge_files_into(&have, h.files);
      if (have.path.empty()) {
        have.path = h.path;
      }
      merged = true;
      break;
    }
    if (merged) {
      continue;
    }
    if (!seen.insert(key).second) {
      continue;
    }
    st->candidatas.push_back(h);
    if (static_cast<int>(st->candidatas.size()) >= kWaveMaxHits) {
      break;
    }
  }
}

std::vector<std::string> wave_needle_search_keys(const std::string& needle) {
  std::vector<std::string> keys;
  if (needle.empty()) {
    return keys;
  }
  keys.push_back(needle);
  const auto pos = needle.rfind("::");
  if (pos == std::string::npos || pos + 2 >= needle.size()) {
    return keys;
  }
  const std::string sym = needle.substr(pos + 2);
  if (sym.size() >= 3 && ascii_lower(sym) != ascii_lower(needle)) {
    keys.push_back(sym);
  }
  return keys;
}

std::string wave_needle_stem_hint(const std::string& needle) {
  const auto pos = needle.rfind("::");
  if (pos == std::string::npos || pos == 0) {
    return {};
  }
  return needle.substr(0, pos);
}

bool wave_line_has_needle(const std::string& line, const std::string& needle) {
  if (needle.empty()) {
    return false;
  }
  auto ident = [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
  };
  const std::string hay = ascii_lower(line);
  const std::string want = ascii_lower(needle);
  std::size_t pos = 0;
  while ((pos = hay.find(want, pos)) != std::string::npos) {
    const bool left_ok = pos == 0 || !ident(static_cast<unsigned char>(hay[pos - 1]));
    const std::size_t end = pos + want.size();
    const bool right_ok = end >= hay.size() || !ident(static_cast<unsigned char>(hay[end]));
    if (left_ok && right_ok) {
      return true;
    }
    ++pos;
  }
  return false;
}

std::vector<std::string> wave_extract_call_names(const std::string& text) {
  static const char* kSkip[] = {
      "if",       "for",      "while",    "switch",   "return",   "sizeof",  "catch",
      "new",      "delete",   "static_cast", "dynamic_cast", "reinterpret_cast",
      "const_cast", "sizeof", "alignof", "typeid", "throw", "try", "else", "do",
      "case",     "default",  "goto",     "co_await", "co_yield", "co_return"};
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  const std::size_t n = text.size();
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!(std::isalpha(c) || c == '_')) {
      continue;
    }
    std::size_t j = i + 1;
    while (j < n) {
      const unsigned char d = static_cast<unsigned char>(text[j]);
      if (!(std::isalnum(d) || d == '_')) {
        break;
      }
      ++j;
    }
    std::string name = text.substr(i, j - i);
    i = j - 1;
    std::size_t k = j;
    while (k < n && (text[k] == ' ' || text[k] == '\t')) {
      ++k;
    }
    if (k >= n || text[k] != '(') {
      continue;
    }
    if (name.size() < 3) {
      continue;
    }
    bool skip = false;
    for (const char* s : kSkip) {
      if (name == s) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }
    const std::string key = ascii_lower(name);
    if (!seen.insert(key).second) {
      continue;
    }
    out.push_back(std::move(name));
    if (static_cast<int>(out.size()) >= kWaveMaxMentions) {
      break;
    }
  }
  return out;
}

bool outgoing_skip_name(const std::string& name) {
  static const char* kSkip[] = {
      "append",     "empty",       "load",       "lock",      "find",      "size",
      "substr",     "compare",     "getline",    "printf",    "sprintf",   "memcpy",
      "memset",     "malloc",      "free",       "make_unique","make_shared","to_string",
      "move",       "forward",     "get",        "at",        "push_back", "emplace_back",
      "insert",     "erase",       "clear",      "begin",     "end",       "swap",
      "isdigit",    "isalnum",     "isalpha",    "tolower",   "toupper",   "string",
      "vector",     "optional",    "ai_trace",   "ai_trace_escape", "ai_trace_configure",
      "ai_trace_status_text", "ai_trace_clear", "ai_trace_path", "ai_trace_tail"};
  const std::string low = ascii_lower(name);
  for (const char* s : kSkip) {
    if (low == s) {
      return true;
    }
  }
  if (low.rfind("ai_trace", 0) == 0) {
    return true;
  }
  if (low.rfind("std", 0) == 0) {
    return true;
  }
  return false;
}

std::string outgoing_compact(std::string s, std::size_t n = 72) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '{' || s.back() == ':')) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  s = s.substr(i);
  if (s.size() > n) {
    s.resize(n);
    s += "…";
  }
  return s;
}

std::string outgoing_when_from_line(const std::string& raw) {
  std::string line = raw;
  const auto bar = line.find('|');
  if (bar != std::string::npos && bar < 8) {
    line = line.substr(bar + 1);
  }
  const std::string low = ascii_lower(line);
  auto pos = low.find("case ");
  if (pos != std::string::npos) {
    return outgoing_compact(line.substr(pos));
  }
  pos = low.find("else if");
  if (pos != std::string::npos) {
    return outgoing_compact(line.substr(pos));
  }
  pos = low.find("if (");
  if (pos != std::string::npos) {
    return outgoing_compact(line.substr(pos));
  }
  pos = low.find("if(");
  if (pos != std::string::npos) {
    return outgoing_compact(line.substr(pos));
  }
  pos = low.find("switch (");
  if (pos != std::string::npos) {
    return outgoing_compact(line.substr(pos));
  }
  return {};
}

std::vector<WaveOutgoingCall> wave_follow_outgoing_calls(const std::string& body) {
  std::vector<WaveOutgoingCall> out;
  std::unordered_set<std::string> seen;
  std::string when;
  std::string line;
  auto flush = [&]() {
    if (line.empty()) {
      return;
    }
    const std::string w = outgoing_when_from_line(line);
    if (!w.empty()) {
      when = w;
    }
    const auto names = wave_extract_call_names(line);
    for (const auto& name : names) {
      if (outgoing_skip_name(name)) {
        continue;
      }
      const std::string key = ascii_lower(name);
      if (!seen.insert(key).second) {
        continue;
      }
      WaveOutgoingCall c;
      c.symbol = name;
      c.when = when;
      out.push_back(std::move(c));
      if (static_cast<int>(out.size()) >= kWaveMaxOutgoing) {
        line.clear();
        return;
      }
    }
    line.clear();
  };
  for (char c : body) {
    if (c == '\n') {
      flush();
      if (static_cast<int>(out.size()) >= kWaveMaxOutgoing) {
        break;
      }
    } else if (c != '\r') {
      line.push_back(c);
    }
  }
  if (static_cast<int>(out.size()) < kWaveMaxOutgoing) {
    flush();
  }
  return out;
}

std::string wave_follow_outgoing_markdown(const std::string& display,
                                          const std::vector<WaveOutgoingCall>& calls) {
  std::ostringstream out;
  out << "----- outgoing " << display << " -----\n";
  if (calls.empty()) {
    out << "(sin callees en este cuerpo)\n";
    return out.str();
  }
  const auto col = display.rfind(':');
  const std::string from =
      (col != std::string::npos && col + 1 < display.size()) ? display.substr(col + 1) : display;
  for (const auto& c : calls) {
    out << "- ";
    if (!c.when.empty()) {
      out << "when `" << c.when << "` then ";
    }
    out << "`" << c.symbol << "`\n";
  }
  out << "```mermaid\nflowchart TD\n";
  for (const auto& c : calls) {
    out << "  " << from << "[\"" << from << "\"] --> " << c.symbol << "[\"" << c.symbol << "\"]\n";
  }
  out << "```\n";
  return out.str();
}

int wave_seed_from_atlas(WaveState* st, const nlohmann::json& payload) {
  if (st == nullptr) {
    return 0;
  }
  const auto zones = payload.value("zones", nlohmann::json::array());
  std::vector<WaveHit> hits;
  std::ostringstream atlas;
  atlas << "# atlas (hipótesis de retrieval; no es veredicto)\n";
  atlas << "n=" << zones.size() << "  peek/juicio sobre estos ids, o needles si el mapa no huele\n";
  int n = 0;
  for (const auto& zone : zones) {
    if (!zone.is_object()) {
      continue;
    }
    WaveHit h;
    h.id = json_str(zone, "id");
    h.kind = json_str(zone, "kind");
    h.needle = "atlas";
    for (const char* key : {"primary_stems", "core_stems"}) {
      const auto stems = json_str_array(zone, key);
      if (!stems.empty()) {
        h.stem = stems.front();
        break;
      }
    }
    nlohmann::json first;
    if (zone.contains("anchors") && zone["anchors"].is_array()) {
      for (const auto& group : zone["anchors"]) {
        if (group.is_array() && !group.empty() && group.front().is_object()) {
          first = group.front();
          break;
        }
        if (group.is_object()) {
          first = group;
          break;
        }
      }
    }
    if (first.is_object()) {
      const std::string target = json_str(first, "target");
      if (!target.empty()) {
        split_path_symbol(target, &h.path, &h.symbol);
      }
      if (h.stem.empty()) {
        h.stem = json_str(first, "stem");
      }
    }
    collect_zone_files(zone, &h.files);
    if (!h.path.empty()) {
      add_path_and_sibling(&h.files, h.path);
    }
    if (h.id.empty()) {
      continue;
    }
    atlas << "\n" << h.id;
    if (!h.kind.empty()) {
      atlas << "  kind=" << h.kind;
    }
    if (!h.stem.empty()) {
      atlas << "  owns=" << h.stem;
    }
    atlas << "\n";
    if (!h.path.empty() && !h.symbol.empty()) {
      atlas << "    peek: " << h.path << ":" << h.symbol << "\n";
    }
    if (!h.files.empty()) {
      atlas << "    files:";
      for (const auto& f : h.files) {
        atlas << " " << f;
      }
      atlas << "\n";
    }
    hits.push_back(std::move(h));
    if (++n >= kWaveMaxHits) {
      break;
    }
  }
  wave_merge_hits(st, hits);
  if (st->atlas_md.empty()) {
    st->atlas_md = atlas.str();
  }
  return static_cast<int>(hits.size());
}

void wave_retain_atlas_ids(WaveState* st, const std::vector<std::string>& ids) {
  if (st == nullptr || ids.empty()) {
    return;
  }
  std::unordered_set<std::string> want;
  for (const auto& id : ids) {
    want.insert(ascii_lower(id));
  }
  std::vector<WaveHit> kept;
  for (const auto& h : st->candidatas) {
    if (h.needle == "atlas") {
      if (!want.count(ascii_lower(h.id)) && !want.count(ascii_lower(wave_hit_key(h)))) {
        continue;
      }
    }
    kept.push_back(h);
  }
  st->candidatas = std::move(kept);
}

int wave_ingest_zone_symbols(WaveState* st, const nlohmann::json& payload,
                             const std::vector<std::string>& zone_ids) {
  if (st == nullptr) {
    return 0;
  }
  std::unordered_set<std::string> want;
  for (const auto& id : zone_ids) {
    want.insert(ascii_lower(id));
  }
  std::vector<WaveHit> hits;
  auto push_loc = [&](const std::string& loc, const std::string& stem, const std::string& kind) {
    if (loc.empty()) {
      return;
    }
    WaveHit h;
    h.kind = kind.empty() ? "fn" : kind;
    h.needle = "ficha";
    h.stem = stem;
    if (!fill_hit_from_locator(&h, loc, stem)) {
      return;
    }
    if (h.id.empty()) {
      if (!h.stem.empty() && !h.symbol.empty()) {
        h.id = h.stem + "::" + h.symbol;
      } else if (!h.path.empty() && !h.symbol.empty()) {
        h.id = h.path + ":" + h.symbol;
      }
    }
    hits.push_back(std::move(h));
  };
  auto push_obj = [&](const nlohmann::json& o, const std::string& zone_stem) {
    if (!o.is_object()) {
      if (o.is_string()) {
        push_loc(o.get<std::string>(), zone_stem, "fn");
      }
      return;
    }
    const std::string stem = json_str(o, "stem").empty() ? zone_stem : json_str(o, "stem");
    const std::string kind = json_str(o, "kind");
    std::string loc = json_str(o, "target");
    if (loc.empty()) {
      loc = json_str(o, "id");
    }
    if (loc.empty()) {
      loc = json_str(o, "from");
    }
    if (!loc.empty()) {
      push_loc(loc, stem, kind);
    }
    const std::string to = json_str(o, "to");
    if (!to.empty() && to.find(':') != std::string::npos) {
      push_loc(to, stem, kind);
    }
  };
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    if (!zone.is_object()) {
      continue;
    }
    const std::string zid = json_str(zone, "id");
    if (!want.empty() && !want.count(ascii_lower(zid))) {
      continue;
    }
    std::string zstem;
    for (const char* key : {"primary_stems", "core_stems"}) {
      const auto stems = json_str_array(zone, key);
      if (!stems.empty()) {
        zstem = stems.front();
        break;
      }
    }
    const auto n0 = hits.size();
    for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
      push_obj(card, zstem);
    }
    const auto roles = zone.value("roles", nlohmann::json::object());
    for (const char* role : {"writers", "readers", "controls", "handoffs"}) {
      if (!roles.contains(role) || !roles[role].is_array()) {
        continue;
      }
      for (const auto& w : roles[role]) {
        push_obj(w, zstem);
      }
    }
    if (zone.contains("anchors") && zone["anchors"].is_array()) {
      for (const auto& group : zone["anchors"]) {
        if (group.is_array()) {
          for (const auto& inner : group) {
            push_obj(inner, zstem);
          }
        } else {
          push_obj(group, zstem);
        }
      }
    }
    if (zone.contains("mechanism") && zone["mechanism"].is_object()) {
      for (const auto& kv : zone["mechanism"].items()) {
        push_obj(kv.value(), zstem);
      }
    }
    for (const auto& port : zone.value("ports", nlohmann::json::array())) {
      push_obj(port, zstem);
    }
    for (const auto& hub : zone.value("hub_nodes", nlohmann::json::array())) {
      push_obj(hub, zstem);
    }
    if (static_cast<int>(hits.size() - n0) > kWaveMaxFichaPerZone) {
      hits.resize(n0 + static_cast<size_t>(kWaveMaxFichaPerZone));
    }
  }
  const auto before = st->candidatas.size();
  wave_merge_hits(st, hits);
  return static_cast<int>(st->candidatas.size() - before);
}

bool wave_needs_cover(const WaveState& st) {
  return st.olas_log.empty() && !st.atlas_md.empty();
}

int wave_cover_restore_caller(WaveOla* ola, const WaveState& st) {
  if (ola == nullptr || ola->do_kind != WaveDo::Juicio) {
    return 0;
  }
  if (!st.olas_log.empty()) {
    return 0;
  }
  auto kind_of = [&](const std::string& id) -> std::string {
    if (const WaveHit* h = wave_find_hit(st.candidatas, id)) {
      return ascii_lower(h->kind);
    }
    return {};
  };
  bool have_latch = false;
  for (const auto& id : ola->keep) {
    const std::string k = kind_of(id);
    if (k == "latch" || k == "object") {
      have_latch = true;
      break;
    }
  }
  if (!have_latch) {
    return 0;
  }
  std::string caller;
  for (const auto& h : st.candidatas) {
    if (h.needle != "atlas" || ascii_lower(h.kind) != "caller" || h.id.empty()) {
      continue;
    }
    bool already = false;
    for (const auto& id : ola->keep) {
      if (id == h.id || locus_keys_match(id, h.id)) {
        already = true;
        break;
      }
    }
    if (!already) {
      caller = h.id;
      break;
    }
  }
  if (caller.empty()) {
    return 0;
  }
  if (static_cast<int>(ola->keep.size()) >= kWaveCoverKeepMax) {
    int evict = -1;
    for (int i = static_cast<int>(ola->keep.size()) - 1; i >= 0; --i) {
      const std::string k = kind_of(ola->keep[static_cast<std::size_t>(i)]);
      if (k != "latch" && k != "object") {
        evict = i;
        break;
      }
    }
    if (evict < 0) {
      evict = static_cast<int>(ola->keep.size()) - 1;
    }
    ola->keep.erase(ola->keep.begin() + evict);
  }
  ola->keep.push_back(caller);
  ola->drop.erase(std::remove_if(ola->drop.begin(), ola->drop.end(),
                                 [&](const std::string& id) {
                                   return id == caller || locus_keys_match(id, caller);
                                 }),
                  ola->drop.end());
  return 1;
}

bool wave_close_audit_accept(const WaveOla& draft, const WaveOla& audit, const WaveState& st) {
  if (!audit.ok) {
    return false;
  }
  if (audit.do_kind == WaveDo::Cerrar) {
    return true;
  }
  auto named_in = [&](const std::string& loc, const std::string& hay) {
    if (loc.empty() || hay.empty()) {
      return false;
    }
    if (hay.find(loc) != std::string::npos) {
      return true;
    }
    const std::string tail = symbol_tail(loc);
    return !tail.empty() && tail.size() >= 4 && hay.find(tail) != std::string::npos;
  };
  auto in_list = [&](const std::vector<std::string>& xs, const std::string& loc) {
    for (const auto& x : xs) {
      if (locus_keys_match(x, loc)) {
        return true;
      }
    }
    return false;
  };
  auto anchored = [&](const std::string& loc) {
    if (loc.empty() || is_file_only_loc(loc)) {
      return false;
    }
    if (named_in(loc, draft.why)) {
      return true;
    }
    for (const auto& h : draft.huecos) {
      if (named_in(loc, h) || locus_keys_match(h, loc)) {
        return true;
      }
    }
    if (wave_find_hit(st.candidatas, loc) != nullptr) {
      return true;
    }
    if (list_has_locus(st.peeks_done, loc, st.candidatas)) {
      return true;
    }
    return in_list(st.circuit_on, loc) || in_list(st.circuit_off, loc) ||
           in_list(st.circuit_on_via, loc) || in_list(st.circuit_off_via, loc) ||
           in_list(st.circuit_callers_on, loc) || in_list(st.circuit_callers_off, loc);
  };
  if (audit.do_kind == WaveDo::Peek) {
    if (audit.peeks.size() != 1) {
      return false;
    }
    const std::string& peek = audit.peeks[0];
    if (list_has_locus(st.peeks_done, peek, st.candidatas)) {
      return false;
    }
    if (!named_in(peek, draft.why)) {
      bool in_hueco = false;
      for (const auto& h : draft.huecos) {
        if (named_in(peek, h) || locus_keys_match(h, peek)) {
          in_hueco = true;
          break;
        }
      }
      if (!in_hueco) {
        return false;
      }
    }
    return true;
  }
  if (audit.do_kind != WaveDo::Follow) {
    return false;
  }
  if (audit.follows.empty() || static_cast<int>(audit.follows.size()) > 2) {
    return false;
  }
  for (const auto& fol : audit.follows) {
    if (list_has_locus(st.follows_done, fol, st.candidatas)) {
      return false;
    }
    if (!anchored(fol)) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> wave_salvage_keep(const std::string& raw,
                                           const std::vector<std::string>& allowed) {
  std::vector<std::string> out;
  if (raw.empty() || allowed.empty()) {
    return out;
  }
  std::unordered_set<std::string> allow(allowed.begin(), allowed.end());
  const std::string lower = ascii_lower(raw);
  auto pos = lower.find("\"keep\"");
  if (pos == std::string::npos) {
    pos = lower.find("keep");
  }
  if (pos == std::string::npos) {
    return out;
  }
  const std::size_t n = std::min(raw.size() - pos, static_cast<std::size_t>(480));
  const std::string window = raw.substr(pos, n);
  for (std::size_t i = 0; i + 1 < window.size(); ++i) {
    if (window[i] != 'M' && window[i] != 'm') {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(window[i + 1]))) {
      continue;
    }
    std::size_t j = i + 1;
    while (j < window.size() && std::isdigit(static_cast<unsigned char>(window[j]))) {
      ++j;
    }
    std::string id = window.substr(i, j - i);
    id[0] = 'M';
    if (!allow.count(id)) {
      i = j - 1;
      continue;
    }
    bool dup = false;
    for (const auto& have : out) {
      if (have == id) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out.push_back(id);
    }
    if (static_cast<int>(out.size()) >= kWaveCoverKeepMax) {
      break;
    }
    i = j - 1;
  }
  return out;
}

WaveOla wave_parse_ola(const std::string& raw) {
  WaveOla out;
  const std::string blob = extract_ola_json(raw);
  if (blob.empty()) {
    out.error = "ola sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(blob);
  } catch (const std::exception& e) {
    out.error = std::string("JSON ola inválido: ") + e.what();
    return out;
  }
  out.raw = j;
  const std::string action = j.value("action", "");
  if (action.size() < 5 || action.compare(0, 5, "ola_v") != 0) {
    out.error = "contrato ola_v1 inválido";
    return out;
  }
  out.do_kind = wave_do_parse(json_str(j, "do"));
  if (out.do_kind == WaveDo::Invalid) {
    out.error = "do inválido";
    return out;
  }
  out.campo = json_str(j, "campo");
  out.needles = json_str_array(j, "needles");
  out.keep = json_str_array(j, "keep");
  out.drop = json_str_array(j, "drop");
  out.peek = json_str(j, "peek");
  out.peeks = json_str_array(j, "peeks");
  out.follows = json_str_or_array(j, "follow");
  if (out.follows.empty()) {
    out.follows = json_str_array(j, "follows");
  }
  collect_peeks(&out);
  collect_follows(&out);
  out.from = json_str(j, "from");
  out.to = json_str(j, "to");
  out.in_locus = json_str(j, "in");
  if (out.from.empty() || out.to.empty()) {
    const auto pair = json_str_array(j, "entre");
    if (pair.size() >= 2) {
      if (out.from.empty()) {
        out.from = pair[0];
      }
      if (out.to.empty()) {
        out.to = pair[1];
      }
    }
  }
  out.why = json_str(j, "why");
  out.huecos = json_str_array(j, "huecos");
  if (out.why.size() < 8) {
    out.error = "why demasiado corto";
    return out;
  }
  if (out.do_kind != WaveDo::Cerrar && out.why.size() > 240) {
    utf8_resize(&out.why, 240);
  } else if (out.why.size() > 800) {
    utf8_resize(&out.why, 800);
  }
  const int parts = (out.needles.empty() ? 0 : 1) + (out.peeks.empty() ? 0 : 1) +
                    (out.follows.empty() ? 0 : 1) +
                    ((out.from.empty() || out.to.empty()) ? 0 : 1);
  if (parts >= 2 && (out.do_kind == WaveDo::Needles || out.do_kind == WaveDo::Peek ||
                     out.do_kind == WaveDo::Follow || out.do_kind == WaveDo::Entre)) {
    out.do_kind = WaveDo::Tanda;
  }
  if (static_cast<int>(out.needles.size()) > kWaveMaxNeedles) {
    out.needles.resize(static_cast<size_t>(kWaveMaxNeedles));
  }
  if (out.do_kind == WaveDo::Needles && out.needles.empty()) {
    out.error = "needles vacío";
    return out;
  }
  if (!out.in_locus.empty() && out.needles.empty()) {
    out.error = "in sin needles";
    return out;
  }
  if (out.do_kind == WaveDo::Peek && out.peeks.empty()) {
    out.error = "peek vacío";
    return out;
  }
  if (out.do_kind == WaveDo::Follow && out.follows.empty()) {
    out.error = "follow vacío";
    return out;
  }
  if (out.do_kind == WaveDo::Entre && (out.from.empty() || out.to.empty())) {
    out.error = "entre sin from/to";
    return out;
  }
  if (out.do_kind == WaveDo::Tanda && out.needles.empty() && out.peeks.empty() &&
      out.follows.empty() && (out.from.empty() || out.to.empty())) {
    out.error = "tanda sin needles, peeks, follows ni entre";
    return out;
  }
  out.ok = true;
  return out;
}

bool wave_check_barriers(const WaveOla& ola, const WaveState& st, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (!ola.ok) {
    return set(ola.error.empty() ? "ola inválida" : ola.error.c_str());
  }
  if (st.done) {
    return set("ya se cerró");
  }
  auto in_ok = [&]() -> const char* {
    if (ola.in_locus.empty()) {
      return nullptr;
    }
    if (loc_is_stem_campo(ola.in_locus, nullptr)) {
      return "in no es un módulo; usa campo o follow del símbolo";
    }
    if (is_file_only_loc(ola.in_locus)) {
      return "in exige símbolo (path:fn), no un archivo";
    }
    if (!loc_resolves_fn(st, ola.in_locus, nullptr, nullptr)) {
      return "in exige símbolo (path:fn), no un archivo";
    }
    if (!peek_is_allowed(st, ola.in_locus)) {
      return "in no está en candidatas, archivos, zonas keep ni menciones";
    }
    return nullptr;
  };
  auto needles_have_new = [&]() {
    if (ola.needles.empty()) {
      return false;
    }
    for (const auto& needle : ola.needles) {
      if (!needle_already_logged(st, needle, ola.in_locus)) {
        return true;
      }
    }
    return false;
  };
  auto peeks_ok = [&]() {
    if (ola.peeks.empty()) {
      return true;
    }
    for (const auto& peek : ola.peeks) {
      if (!peek_is_allowed(st, peek)) {
        return false;
      }
    }
    return true;
  };
  auto follows_ok = [&]() {
    if (ola.follows.empty()) {
      return true;
    }
    for (const auto& loc : ola.follows) {
      if (!peek_is_allowed(st, loc)) {
        return false;
      }
    }
    return true;
  };
  auto peeks_have_new = [&]() {
    for (const auto& peek : ola.peeks) {
      if (!list_has_locus(st.peeks_done, peek, st.candidatas)) {
        return true;
      }
    }
    return false;
  };
  auto follows_have_new = [&]() {
    for (const auto& loc : ola.follows) {
      if (!list_has_locus(st.follows_done, loc, st.candidatas)) {
        return true;
      }
    }
    return false;
  };
  if (ola.do_kind == WaveDo::Needles) {
    if (ola.needles.empty()) {
      return true;
    }
    if (!ola.in_locus.empty()) {
      if (const char* m = in_ok()) {
        return set(m);
      }
    }
    if (!needles_have_new()) {
      return set(ola.in_locus.empty() ? "needles ya tirados" : "in ya grep en este cuerpo");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Juicio) {
    if (st.candidatas.empty()) {
      return set("juicio sin candidatas");
    }
    if (ola.keep.empty() && ola.drop.empty()) {
      return set("juicio sin keep ni drop");
    }
    for (const auto& id : ola.keep) {
      if (!wave_id_in_hits(st.candidatas, id)) {
        return set("keep no está en candidatas");
      }
    }
    for (const auto& id : ola.drop) {
      if (!wave_id_in_hits(st.candidatas, id)) {
        return set("drop no está en candidatas");
      }
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Peek) {
    if (ola.peeks.size() <= 1) {
      if (!peeks_ok()) {
        return set("peek no está en candidatas, archivos, zonas keep ni menciones");
      }
    } else {
      bool any = false;
      for (const auto& peek : ola.peeks) {
        if (peek_is_allowed(st, peek)) {
          any = true;
          break;
        }
      }
      if (!any) {
        return set("peek no está en candidatas, archivos, zonas keep ni menciones");
      }
    }
    if (!ola.peeks.empty() && !peeks_have_new()) {
      return set("peek ya leído");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Follow) {
    if (!follows_ok()) {
      return set("follow no está en candidatas, archivos, zonas keep ni menciones");
    }
    if (!ola.follows.empty() && !follows_have_new()) {
      return set("follow ya hecho; stacks en Último follow / ya seguidos");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Entre) {
    if (ola.from.empty() || ola.to.empty()) {
      return set("entre sin from/to");
    }
    if (!peek_is_allowed(st, ola.from) || !peek_is_allowed(st, ola.to)) {
      return set("entre no está en candidatas, archivos, zonas keep ni menciones");
    }
    if (entre_already_done(st, ola.from, ola.to)) {
      return set("entre ya pedido");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Tanda) {
    const bool new_needles = needles_have_new();
    const bool new_peeks = !ola.peeks.empty() && peeks_have_new();
    const bool new_follows = !ola.follows.empty() && follows_have_new();
    const bool new_entre =
        !ola.from.empty() && !ola.to.empty() && !entre_already_done(st, ola.from, ola.to);
    if (!ola.needles.empty() && !new_needles && !new_peeks && !new_follows && !new_entre) {
      return set("needles ya tirados");
    }
    if (!new_needles && !new_peeks && !new_follows && !new_entre) {
      if (!ola.follows.empty()) {
        return set("follow ya hecho; stacks en Último follow / ya seguidos");
      }
      if (!ola.peeks.empty()) {
        return set("peek ya leído");
      }
      if (!ola.from.empty()) {
        return set("entre ya pedido");
      }
      return set("tanda vacía");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Cerrar) {
    return true;
  }
  return set("do inválido");
}

bool wave_apply(WaveState* st, const WaveOla& ola_in, const WaveOps& ops, std::string* err) {
  if (st == nullptr) {
    if (err) {
      *err = "estado nulo";
    }
    return false;
  }
  WaveOla ola = ola_in;
  normalize_ola_in(&ola, *st);
  std::string barrier;
  if (!wave_check_barriers(ola, *st, &barrier)) {
    st->last_error = barrier;
    if (err) {
      *err = barrier;
    }
    return false;
  }
  st->last_error.clear();
  if (!ola.campo.empty()) {
    st->campo = ola.campo;
  }
  if (ola.do_kind == WaveDo::Needles || ola.do_kind == WaveDo::Tanda ||
      ola.do_kind == WaveDo::Peek || ola.do_kind == WaveDo::Follow ||
      ola.do_kind == WaveDo::Entre) {
    std::string needle_detail;
    std::string peek_detail;
    std::string follow_detail;
    std::string entre_detail;
    int peek_ok = 0;
    int follow_ok = 0;
    int entre_ok = 0;
    if (!ola.needles.empty() &&
        (ola.do_kind == WaveDo::Needles || ola.do_kind == WaveDo::Tanda)) {
      std::string nerr;
      if (!apply_needles(st, ola.needles, ops, &needle_detail, &nerr, true, ola.in_locus)) {
        st->last_error = nerr;
        if (ola.do_kind != WaveDo::Tanda) {
          if (err) {
            *err = nerr;
          }
          return false;
        }
      }
    }
    if (!ola.peeks.empty() &&
        (ola.do_kind == WaveDo::Peek || ola.do_kind == WaveDo::Tanda)) {
      std::string perr;
      const bool skip_fail = ola.do_kind == WaveDo::Tanda || ola.peeks.size() > 1;
      if (!apply_peeks(st, ola.peeks, ops, skip_fail, &peek_ok, &peek_detail, &perr)) {
        st->last_error = perr;
        if (err) {
          *err = perr;
        }
        return false;
      }
    }
    if (!ola.follows.empty() &&
        (ola.do_kind == WaveDo::Follow || ola.do_kind == WaveDo::Tanda)) {
      std::string ferr;
      const bool skip_fail = ola.do_kind == WaveDo::Tanda || ola.follows.size() > 1;
      if (!apply_follows(st, ola.follows, ops, skip_fail, &follow_ok, &follow_detail, &ferr)) {
        st->last_error = ferr;
        if (err) {
          *err = ferr;
        }
        return false;
      }
    }
    if (!ola.from.empty() && !ola.to.empty() &&
        (ola.do_kind == WaveDo::Entre || ola.do_kind == WaveDo::Tanda)) {
      std::string eerr;
      const bool skip_fail = ola.do_kind == WaveDo::Tanda;
      if (!apply_entre(st, ola.from, ola.to, ops, skip_fail, &entre_ok, &entre_detail, &eerr)) {
        st->last_error = eerr;
        if (err) {
          *err = eerr;
        }
        return false;
      }
    }
    if (ola.do_kind == WaveDo::Tanda && needle_detail.empty() && peek_ok == 0 && follow_ok == 0 &&
        entre_ok == 0) {
      st->last_error = st->last_error.empty() ? "tanda no aplicó nada" : st->last_error;
      if (err) {
        *err = st->last_error;
      }
      return false;
    }
    if (peek_ok > 0 || follow_ok > 0 || entre_ok > 0 || !needle_detail.empty()) {
      st->last_error.clear();
    }
    std::string detail = needle_detail;
    auto append_det = [&](const std::string& s) {
      if (s.empty()) {
        return;
      }
      if (!detail.empty()) {
        detail += "; ";
      }
      detail += s;
    };
    append_det(peek_detail);
    append_det(follow_detail);
    append_det(entre_detail);
    const char* name = wave_do_name(ola.do_kind);
    push_ola_log(st, name, ola.why, detail);
    maybe_auto_entre(st, ops);
    return true;
  }
  if (ola.do_kind == WaveDo::Juicio) {
    wave_cover_restore_caller(&ola, *st);
    auto upsert = [&](const std::string& id, const char* verdict) {
      const WaveHit* hit = wave_find_hit(st->candidatas, id);
      const std::string key = hit ? wave_hit_key(*hit) : id;
      for (auto& z : st->zonas) {
        if (z.id == key || z.id == id) {
          z.verdict = verdict;
          return;
        }
      }
      WaveZone z;
      z.id = key;
      z.verdict = verdict;
      st->zonas.push_back(std::move(z));
    };
    for (const auto& id : ola.drop) {
      upsert(id, "drop");
    }
    for (const auto& id : ola.keep) {
      upsert(id, "keep");
    }
    push_ola_log(st, "juicio", ola.why,
                 "keep=" + std::to_string(ola.keep.size()) +
                     " drop=" + std::to_string(ola.drop.size()));
    return true;
  }
  st->done = true;
  st->cierre = ola.why;
  st->huecos_claimed = ola.huecos;
  wave_attach_cierre_caption(st);
  push_ola_log(st, "cerrar", ola.why, {});
  return true;
}

bool wave_circuit_complete(const WaveState& st) {
  return !st.circuit_on.empty() && !st.circuit_off.empty();
}

bool wave_is_last_propose(const WaveState& st) {
  if (st.propose_n > 0) {
    return st.propose_n >= kWaveMaxWaves;
  }
  return st.wave_n >= kWaveMaxWaves;
}

std::string wave_circuit_markdown(const WaveState& st) {
  std::ostringstream out;
  out << "## Circuito\n";
  if (st.circuit_on.empty() && st.circuit_off.empty()) {
    out << "(aún no hay ON/OFF anclados)\n";
  } else {
  out << "ON:  " << (st.circuit_on.empty() ? "(falta)" : join_list(st.circuit_on)) << "\n";
  if (!st.circuit_on_via.empty()) {
    out << "via ON:  " << join_list(st.circuit_on_via) << "\n";
  }
  out << "OFF: " << (st.circuit_off.empty() ? "(falta)" : join_list(st.circuit_off)) << "\n";
  if (!st.circuit_off_via.empty()) {
    out << "via OFF: " << join_list(st.circuit_off_via) << "\n";
  }
  if (!st.circuit_callers_on.empty()) {
    out << "callers ON:  " << join_list(st.circuit_callers_on) << "\n";
  }
  if (!st.circuit_callers_off.empty()) {
    out << "callers OFF: " << join_list(st.circuit_callers_off) << "\n";
  }
  if (!st.circuit_entre.empty()) {
    out << "entre: " << st.circuit_entre << "\n";
  }
  if (wave_circuit_complete(st)) {
      out << "estado: completo — preferí cerrar a otro grep\n";
    } else {
      out << "estado: incompleto\n";
    }
  }
  const std::string sketch = wave_sketch_markdown(st);
  if (!sketch.empty()) {
    out << sketch;
  }
  return out.str();
}

std::vector<WaveSketchLink> wave_sketch_edges(const WaveState& st) {
  std::vector<WaveSketchLink> edges;
  auto node = [&](const std::string& loc) -> std::string {
    if (loc.empty() || is_file_only_loc(loc) || neighbor_id_skip(loc)) {
      return {};
    }
    if (const WaveHit* h = wave_find_hit(st.candidatas, loc)) {
      if (!h->path.empty() && !h->symbol.empty()) {
        return h->path + ":" + h->symbol;
      }
      if (!h->symbol.empty() && !neighbor_id_skip(h->symbol)) {
        return h->symbol;
      }
    }
    if (loc.find('/') != std::string::npos) {
      return loc;
    }
    const auto col = loc.rfind("::");
    if (col != std::string::npos && col + 2 < loc.size()) {
      return loc.substr(col + 2);
    }
    return loc;
  };
  auto lit = [&](const std::string& loc) {
    return list_has_locus(st.peeks_done, loc, st.candidatas) ||
           list_has_locus(st.follows_done, loc, st.candidatas);
  };
  for (const auto& n : st.peek_neighbors) {
    const std::string here = node(n.loc);
    if (here.empty() || !lit(n.loc)) {
      continue;
    }
    for (const auto& c : n.callees) {
      if (lit(c)) {
        sketch_push(&edges, here, node(c), "call");
      }
    }
    for (const auto& c : n.callers) {
      if (lit(c)) {
        sketch_push(&edges, node(c), here, "call");
      }
    }
  }
  for (const auto& l : st.follow_links) {
    if (lit(l.from) && lit(l.to)) {
      sketch_push(&edges, node(l.from), node(l.to), "follow");
    }
  }
  for (const auto& key : st.entres_done) {
    const auto tab = key.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    const std::string from = key.substr(0, tab);
    const std::string to = key.substr(tab + 1);
    if (lit(from) && lit(to)) {
      sketch_push(&edges, node(from), node(to), "entre");
    }
  }
  return edges;
}

std::string wave_sketch_markdown(const WaveState& st) {
  auto node = [&](const std::string& loc) -> std::string {
    if (loc.empty() || is_file_only_loc(loc) || neighbor_id_skip(loc)) {
      return {};
    }
    if (const WaveHit* h = wave_find_hit(st.candidatas, loc)) {
      if (!h->path.empty() && !h->symbol.empty()) {
        return h->path + ":" + h->symbol;
      }
      if (!h->symbol.empty() && !neighbor_id_skip(h->symbol)) {
        return h->symbol;
      }
    }
    if (loc.find('/') != std::string::npos) {
      return loc;
    }
    const auto col = loc.rfind("::");
    if (col != std::string::npos && col + 2 < loc.size()) {
      return loc.substr(col + 2);
    }
    return loc;
  };
  auto lit = [&](const std::string& loc) {
    return list_has_locus(st.peeks_done, loc, st.candidatas) ||
           list_has_locus(st.follows_done, loc, st.candidatas);
  };
  std::vector<std::string> lit_nodes;
  auto add_lit = [&](const std::string& loc) {
    const std::string n = node(loc);
    if (n.empty()) {
      return;
    }
    for (const auto& have : lit_nodes) {
      if (locus_keys_match(have, n)) {
        return;
      }
    }
    lit_nodes.push_back(n);
  };
  for (const auto& p : st.peeks_done) {
    add_lit(p);
  }
  for (const auto& f : st.follows_done) {
    add_lit(f);
  }
  if (lit_nodes.size() < 2) {
    return {};
  }
  const auto edges = wave_sketch_edges(st);
  std::ostringstream out;
  out << "bosquejo (leído; sin arista = islas):\n";
  for (const auto& e : edges) {
    if (e.from.empty() || e.to.empty()) {
      continue;
    }
    out << "  " << e.from << " → " << e.to << "  (" << e.via << ")\n";
  }
  std::vector<std::string> islands;
  for (const auto& p : lit_nodes) {
    bool connected = false;
    for (const auto& e : edges) {
      if (locus_keys_match(e.from, p) || locus_keys_match(e.to, p)) {
        connected = true;
        break;
      }
    }
    if (!connected) {
      islands.push_back(p);
    }
  }
  if (!islands.empty()) {
    out << "islas:";
    for (const auto& i : islands) {
      out << " `" << i << "`";
    }
    out << "\n";
  }
  std::vector<std::string> huecos;
  auto push_hueco = [&](const std::string& c) {
    if (c.empty() || lit(c) || neighbor_id_skip(c)) {
      return;
    }
    const std::string n = node(c).empty() ? c : node(c);
    for (const auto& have : huecos) {
      if (locus_keys_match(have, n)) {
        return;
      }
    }
    if (static_cast<int>(huecos.size()) < 3) {
      huecos.push_back(n);
    }
  };
  for (const auto& c : st.circuit_callers_on) {
    push_hueco(c);
  }
  for (const auto& c : st.circuit_callers_off) {
    push_hueco(c);
  }
  if (!huecos.empty()) {
    out << "hueco:";
    for (const auto& h : huecos) {
      out << " `" << h << "`";
    }
    out << "\n";
  }
  return out.str();
}

std::string wave_circuit_cierre(const WaveState& st) {
  std::ostringstream out;
  out << "ON " << join_list(st.circuit_on);
  if (!st.circuit_on_via.empty()) {
    out << " via " << join_list(st.circuit_on_via);
  }
  out << ". OFF " << join_list(st.circuit_off);
  if (!st.circuit_off_via.empty()) {
    out << " via " << join_list(st.circuit_off_via);
  }
  out << ".";
  if (!st.circuit_callers_on.empty()) {
    out << " Callers ON: " << join_list(st.circuit_callers_on) << ".";
  }
  if (!st.circuit_entre.empty()) {
    out << " entre " << st.circuit_entre << ".";
  }
  return out.str();
}

std::string wave_opened_brief(const std::string& opened_md) {
  if (opened_md.empty()) {
    return {};
  }
  auto p = opened_md.find("# causal_judge");
  if (p == std::string::npos) {
    p = opened_md.find("\n## M");
  }
  if (p != std::string::npos) {
    return opened_md.substr(0, p);
  }
  return wave_clip_head_tail(opened_md, 1800);
}

bool atlas_zone_line_id(const std::string& line, std::string* id) {
  if (line.size() < 2 || line[0] != 'M' || !std::isdigit(static_cast<unsigned char>(line[1]))) {
    return false;
  }
  std::size_t i = 1;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) {
    return false;
  }
  if (id != nullptr) {
    *id = line.substr(0, i);
  }
  return true;
}

std::string wave_atlas_rest_markdown(const std::string& atlas_md,
                                     const std::vector<std::string>& keep_ids) {
  if (atlas_md.empty() || keep_ids.empty()) {
    return {};
  }
  std::unordered_set<std::string> keep;
  for (const auto& id : keep_ids) {
    keep.insert(ascii_lower(id));
  }
  std::ostringstream out;
  std::string cur;
  std::string cur_id;
  auto flush = [&]() {
    if (cur.empty() || cur_id.empty()) {
      cur.clear();
      cur_id.clear();
      return;
    }
    if (keep.count(ascii_lower(cur_id))) {
      cur.clear();
      cur_id.clear();
      return;
    }
    out << cur;
    if (cur.back() != '\n') {
      out << "\n";
    }
    cur.clear();
    cur_id.clear();
  };
  std::istringstream in(atlas_md);
  std::string line;
  while (std::getline(in, line)) {
    std::string id;
    if (atlas_zone_line_id(line, &id)) {
      flush();
      cur_id = std::move(id);
      cur = line;
      cur.push_back('\n');
      continue;
    }
    if (line.rfind("bridges:", 0) == 0 || line.rfind("holes:", 0) == 0 ||
        line.rfind("<!--", 0) == 0) {
      flush();
      continue;
    }
    if (!cur.empty()) {
      cur += line;
      cur.push_back('\n');
    }
  }
  flush();
  std::string s = out.str();
  if (s.size() > 1800) {
    s = wave_clip_head_tail(s, 1800);
  }
  return s;
}

std::vector<std::string> wave_cover_peek_targets(const std::string& opened_md) {
  std::vector<std::string> raw;
  std::istringstream in(opened_md);
  std::string line;
  auto push_tok = [&](std::string t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) {
      t.erase(t.begin());
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == ',')) {
      t.pop_back();
    }
    if (t.size() < 3) {
      return;
    }
    if (is_file_only_loc(t)) {
      return;
    }
    push_unique_str(&raw, t);
  };
  while (std::getline(in, line)) {
    auto p = line.find("peek:");
    if (p != std::string::npos) {
      std::string rest = line.substr(p + 5);
      std::string cur;
      for (char c : rest) {
        if (c == ',') {
          push_tok(cur);
          cur.clear();
        } else {
          cur.push_back(c);
        }
      }
      push_tok(cur);
    }
    p = line.find("port:");
    if (p != std::string::npos) {
      const auto arrow = line.find("->");
      if (arrow != std::string::npos && arrow + 2 < line.size()) {
        push_tok(line.substr(arrow + 2));
      }
    }
  }
  if (static_cast<int>(raw.size()) > kWaveCoverPeekMax) {
    raw.resize(static_cast<size_t>(kWaveCoverPeekMax));
  }
  return raw;
}

std::string wave_work_markdown(const WaveState& st) {
  std::ostringstream out;
  out << "# cuaderno (trabajo)\n";
  out << "propose=" << st.propose_n << " wave_n=" << st.wave_n
      << " done=" << (st.done ? "yes" : "no") << "\n";
  if (!st.campo.empty()) {
    out << "campo: " << st.campo << "\n";
  }
  if (!st.last_error.empty()) {
    out << "last_error: " << st.last_error << "\n";
    if (st.last_error.find("in ya grep") != std::string::npos ||
        st.last_error.find("in no es un módulo") != std::string::npos ||
        st.last_error.find("in exige símbolo") != std::string::npos) {
      out << "Siguiente legal: follow de un símbolo anclado, in de OTRO path:fn, "
             "peek no leído. No repitas este in. campo solo con la clave JSON campo.\n";
    } else if (st.last_error.find("follow ya hecho") != std::string::npos ||
               st.last_error.find("peek ya leído") != std::string::npos ||
               st.last_error.find("entre ya pedido") != std::string::npos) {
      out << "Siguiente legal: peek/follow/entre de un locus que no esté en ya leídos "
             "ni ya seguidos. Peeks y Follows se acumulan abajo; no vuelvas a pedir "
             "el mismo símbolo.\n";
    }
  }
  out << "\n" << wave_circuit_markdown(st);
  out << "\n## Diario\n";
  if (st.olas_log.empty()) {
    out << "(vacío)\n";
  } else {
    for (const auto& e : st.olas_log) {
      out << "- ola " << e.n << " " << e.do_name;
      if (!e.detail.empty()) {
        out << "  " << e.detail;
      }
      if (!e.why.empty()) {
        out << "  — " << e.why;
      }
      out << "\n";
    }
  }
  out << "\n## Needles (" << st.needles_log.size() << ")\n";
  if (st.needles_log.empty()) {
    out << "(ninguna)\n";
  } else {
    for (const auto& rec : st.needles_log) {
      out << "- " << rec.needle;
      if (!rec.in_locus.empty()) {
        out << " in `" << rec.in_locus << "`";
      }
      out << "  hits=" << rec.hits << "  +" << rec.added;
      if (rec.hits == 0) {
        if (!rec.in_locus.empty()) {
          out << "  (sin match en este cuerpo)";
        } else if (looks_like_field_needle(rec.needle)) {
          out << "  (campo, no nodo; grep `in` en un peek)";
        } else {
          out << "  (sin nodo; no repitas esta grafía)";
        }
      }
      if (!rec.ids.empty()) {
        out << "  loci:";
        for (const auto& id : rec.ids) {
          out << " `" << id << "`";
        }
      }
      out << "\n";
    }
  }
  const std::string peeks_md = collect_peek_sections(st.notas);
  if (!peeks_md.empty() || !st.peeks_done.empty()) {
    out << "\n## Peeks\n";
    if (!peeks_md.empty()) {
      out << peeks_md;
      if (peeks_md.back() != '\n') {
        out << "\n";
      }
    }
    emit_done_loci(out, "ya leídos:", st.peeks_done);
  }

  const std::string follows_md =
      collect_marked_sections(st.follow_md, "----- follow ", kWaveWorkFollowChars);
  if (!follows_md.empty() || !st.follows_done.empty() || !st.entres_done.empty()) {
    out << "\n## Follows\n";
    if (!follows_md.empty()) {
      out << follows_md;
      if (follows_md.back() != '\n') {
        out << "\n";
      }
    }
    emit_done_loci(out, "ya seguidos:", st.follows_done);
    emit_done_loci(out, "ya entre:", st.entres_done);
  }

  const std::string entre_md =
      collect_marked_sections(st.follow_md, "----- entre ", kWaveWorkFollowChars);
  if (!entre_md.empty()) {
    out << "\n## Entre\n" << entre_md;
    if (entre_md.back() != '\n') {
      out << "\n";
    }
  }

  const std::string in_md = collect_marked_sections(st.follow_md, "----- in ", kWaveWorkInChars);
  if (!in_md.empty()) {
    out << "\n## in\n" << in_md;
    if (in_md.back() != '\n') {
      out << "\n";
    }
  }

  int nh = 0;
  for (const auto& h : st.candidatas) {
    if (h.needle != "follow" && h.needle != "entre") {
      continue;
    }
    if (nh == 0) {
      out << "\n## Hops\n";
    }
    out << "- " << wave_hit_key(h);
    if (!h.path.empty() && !h.symbol.empty()) {
      out << "  " << h.path << ":" << h.symbol;
    }
    out << "\n";
    if (++nh >= 8) {
      break;
    }
  }
  constexpr std::size_t kOpenedWorkChars = 2200;
  std::string opened = st.opened_md;
  if (opened.size() > kOpenedWorkChars) {
    opened = wave_clip_head_tail(opened, kOpenedWorkChars);
  }
  if (!opened.empty()) {
    out << "\n## Abiertos\n" << opened;
    if (opened.back() != '\n') {
      out << "\n";
    }
  }
  const std::string rest = wave_atlas_rest_markdown(st.atlas_md, st.opened_ids);
  if (!rest.empty()) {
    out << "\n## Atlas (resto)\n" << rest;
    if (rest.back() != '\n') {
      out << "\n";
    }
  }
  out << "\n## Candidatas\n";
  int n = 0;
  for (const auto& h : st.candidatas) {
    if (h.needle == "atlas") {
      continue;
    }
    out << "- " << wave_hit_key(h);
    if (!h.stem.empty()) {
      out << " stem=" << h.stem;
    }
    if (!h.path.empty() && !h.symbol.empty()) {
      out << "  " << h.path << ":" << h.symbol;
    }
    out << "\n";
    if (++n >= 12) {
      break;
    }
  }
  if (n == 0) {
    out << "(ninguna — primero needles o cover)\n";
  }
  return out.str();
}

std::string wave_notebook_markdown(const WaveState& st) {
  std::ostringstream out;
  out << "# cuaderno olas\n";
  out << "wave_n=" << st.wave_n << " propose=" << st.propose_n
      << " done=" << (st.done ? "yes" : "no") << "\n";
  if (!st.campo.empty()) {
    out << "campo: " << st.campo << "\n";
  }
  if (!st.last_error.empty()) {
    out << "last_error: " << st.last_error << "\n";
  }
  out << "\n" << wave_circuit_markdown(st);
  out << "\n## Diario\n";
  if (st.olas_log.empty()) {
    out << "(vacío)\n";
  } else {
    for (const auto& e : st.olas_log) {
      out << "- ola " << e.n << " " << e.do_name;
      if (!e.detail.empty()) {
        out << "  " << e.detail;
      }
      if (!e.why.empty()) {
        out << "  — " << e.why;
      }
      out << "\n";
    }
  }
  if (!st.mencionados.empty()) {
    out << "menciones (peekables):";
    for (const auto& m : st.mencionados) {
      out << " " << m;
    }
    out << "\n";
  }
  if (!st.opened_md.empty()) {
    out << "\n## Abiertos\n" << st.opened_md;
    if (st.opened_md.back() != '\n') {
      out << "\n";
    }
  }
  out << "\n## Needles (" << st.needles_log.size() << " órdenes)\n";
  if (st.needles_log.empty()) {
    out << "(ninguna — el registry no olvida; anota cada aguja)\n";
  } else {
    for (const auto& rec : st.needles_log) {
      out << "- " << rec.needle;
      if (!rec.in_locus.empty()) {
        out << " in `" << rec.in_locus << "`";
      }
      out << "  hits=" << rec.hits << "  +" << rec.added;
      if (rec.hits == 0) {
        if (!rec.in_locus.empty()) {
          out << "  (sin match en este cuerpo)";
        } else if (looks_like_field_needle(rec.needle)) {
          out << "  (campo, no nodo; grep `in` en un peek)";
        } else {
          out << "  (sin nodo; no repitas esta grafía)";
        }
      }
      out << "\n";
      for (const auto& id : rec.ids) {
        out << "    " << id << "\n";
      }
    }
  }
  out << "\n## Notas peek\n";
  out << (st.notas.empty() ? "(vacío)\n" : st.notas);
  if (st.notas.empty() || st.notas.back() != '\n') {
    out << "\n";
  }
  if (!st.follow_md.empty()) {
    out << "\n## Causal\n" << st.follow_md;
    if (st.follow_md.back() != '\n') {
      out << "\n";
    }
  }
  out << "\n## Candidatas (" << st.candidatas.size() << ")\n";
  if (st.candidatas.empty()) {
    out << "(ninguna — primero needles)\n";
  } else {
    auto emit = [&](bool atlas_only) {
      int n = 0;
      for (const auto& h : st.candidatas) {
        const bool atlas = h.needle == "atlas";
        if (atlas != atlas_only) {
          continue;
        }
        out << "- " << wave_hit_key(h);
        if (!h.kind.empty()) {
          out << " kind=" << h.kind;
        }
        if (!h.stem.empty()) {
          out << " stem=" << h.stem;
        }
        if (!h.needle.empty()) {
          out << " via=" << h.needle;
        }
        if (!atlas && !h.path.empty() && !h.symbol.empty() &&
            ascii_lower(wave_hit_key(h)) != ascii_lower(h.path + ":" + h.symbol)) {
          out << "  " << h.path << ":" << h.symbol;
        }
        out << "\n";
        if (atlas) {
          if (!h.files.empty()) {
            out << "  files:";
            for (const auto& f : h.files) {
              out << " " << f;
            }
            out << "\n";
          } else if (!h.path.empty()) {
            out << "  files: " << h.path << "\n";
          }
        }
        if (++n >= kWaveMaxHits) {
          break;
        }
      }
    };
    emit(false);
    emit(true);
  }
  out << "\n## Zonas\n";
  if (st.zonas.empty()) {
    out << "(ninguna — juicio keep/drop)\n";
  } else {
    for (const auto& z : st.zonas) {
      out << "- " << z.verdict << " " << z.id << "\n";
    }
  }
  if (!st.atlas_md.empty()) {
    out << "\n## Atlas\n" << st.atlas_md;
    if (st.atlas_md.back() != '\n') {
      out << "\n";
    }
  }
  if (!st.cierre.empty()) {
    out << "\n## Cierre\n" << st.cierre << "\n";
    out << "\n" << wave_pack_markdown(st);
  }
  return out.str();
}

std::string wave_cover_system_prompt() {
  return R"(Elige 1–2 ids M* del atlas cuyo owns/nucleus cubra el objeto de la consulta
(latch + caller que lo enciende). Keep AMBOS si hay latch y caller.
PROHIBIDO drop del caller porque "no posee" el LED. Un 3º solo si es hilo rival.
PROHIBIDO chrome/holes. PROHIBIDO inventar ids.
PROHIBIDO prosa, recap, traducir el atlas o repetir estas reglas.

El primer carácter de la respuesta es `{`. Nada antes. Un solo JSON:
{"action":"ola_v1","do":"juicio","keep":["M1","M7"],"drop":["M3"],"why":"latch y caller cubren el objeto"}
)";
}

std::string wave_cover_user_prompt(const WaveState& st) {
  std::ostringstream out;
  out << "Consulta:\n" << st.prompt << "\n\n";
  out << st.atlas_md;
  if (!st.atlas_md.empty() && st.atlas_md.back() != '\n') {
    out << "\n";
  }
  out << "\nJSON ahora. Primer carácter `{`.\n";
  return out.str();
}

std::string wave_pilot_system_prompt() {
  return R"(Eres el PILOTO de exploración. NO editas código. NO lanzas workers.
Cada respuesta es UNA ola. Tras ver el cuaderno eliges el siguiente gesto.
PROHIBIDO un plan congelado de varias olas. PROHIBIDO inventar ids.

El cuaderno de trabajo es la evidencia. Peeks y Follows se ACUMULAN (cuerpos, stacks y recortes). Atlas es hipótesis de retrieval. Mermaid no está aquí.
Si un peek/follow ya está en ya leídos / ya seguidos, léelo en Peeks/Follows; NO lo pidas otra vez.
Peek vale sobre ids, símbolos, menciones, hops, o un archivo listado en files (el header suele bastar para ver la API).

do:
- needles: agujas de MECANISMO (símbolos, APIs), no sinónimos del prompt. El runtime busca substring en id/path/symbol/stem. `stem::simbolo` busca el símbolo recortado a ese stem.
- `in`: locus ya anclado, SIEMPRE un símbolo (`path.cpp:fn` o el nombre de la función). PROHIBIDO `in` de un .cpp/.hpp suelto y PROHIBIDO `in":"stem::módulo"` (eso es `campo`). Con `in`, grep DENTRO de ese cuerpo. hits=0 cuenta. Un campo (`foo_`) no es un nodo del grafo.
- cerrar: tú decides cuándo termina. Si entendiste el objeto (o qué falta), cierra. El runtime no te retiene porque Circuito esté incompleto.
- No repitas needles que ya están en el cuaderno. `foo` y `stem::foo` son la misma aguja. Si hits=0 en el grafo, cambia de SÍMBOLO, no de cualificación. Un grep `in` distinto del mismo keyword sí vale.
- No repitas un peek o follow que ya está en ya leídos / ya seguidos. Llamadores = hops del follow, no otro follow del mismo símbolo.
- juicio: keep/drop solo ids que aparecen en Candidatas (vale M1 del atlas).
- peek: leer 1–3 loci. Función = cuerpo (si es largo: firma + cola). Header = API del .hpp. Bajo el cuerpo: callers (stems keep, pueden cruzar) y calls (del cuerpo). Aguas mismo-stem son incompletas; el flujo es follow.
- follow: callers (quién llama) Y callees (qué llama, con cond). Stacks, ramas ON/CXL/OFF, mermaid. Hops peekables. No es el grep ni la firma del peek.
- entre: camino dirigido en el registry entre DOS loci ya anclados (`from` → `to`). No es el mermaid de follow. `sin camino` también es evidencia. Hops intermedios peekables. Si no sale, prueba el inverso.
- tanda: en UNA ola, needles y/o peeks y/o follows y/o entre. Tras cover: 1 latch + 1 caller (port), NO tres funciones del mismo LED. Un `in` malo no cancela los peeks.
- cerrar: síntesis de lo entendido / lo que falta. Termina. Legal en cualquier ola de piloto. `huecos` opcional: nombres que afirmas y no leíste (no es una lista de deberes). No recetes un parche en un símbolo no leído.

El bosquejo del Circuito une lo ya leído (peek o follow). Sin arista = islas. PROHIBIDO inventar el camino.
- follow: callers (quién llama) Y callees (qué llama, con cond). Stacks, ramas ON/CXL/OFF, mermaid. Hops peekables. No es el grep ni la firma del peek.
- entre: camino dirigido en el registry entre DOS loci ya anclados (`from` → `to`). No es el mermaid de follow. `sin camino` también es evidencia. Hops intermedios peekables. Si no sale, prueba el inverso.
- tanda: en UNA ola, needles y/o peeks y/o follows y/o entre. Tras cover: 1 latch + 1 caller (port), NO tres funciones del mismo LED. Un `in` malo no cancela los peeks.
- cerrar: síntesis de lo entendido / lo que falta. Termina. Legal en cualquier ola de piloto. `huecos` opcional: nombres que afirmas y no leíste (no es una lista de deberes). No recetes un parche en un símbolo no leído.

Campo opcional recorta el grep (stem o prefijo de path).

JSON:
{"action":"ola_v1","do":"needles","needles":["start_job"],"why":"cazar el arranque del objeto"}
{"action":"ola_v1","do":"needles","in":"src/pkg/mod.cpp:run_job","needles":["stop_job","start_job"],"why":"¿todos los returns paran el trabajo?"}
{"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M2"],"why":"esta zona cubre el objeto de la consulta"}
{"action":"ola_v1","do":"peek","peeks":["M1","src/pkg/mod.hpp"],"why":"cuerpo del ancla y API del header"}
{"action":"ola_v1","do":"follow","follows":["M1"],"why":"flujo: quién llama y a quién llama"}
{"action":"ola_v1","do":"entre","from":"start_job","to":"stop_job","why":"hay camino del arranque a la parada"}
{"action":"ola_v1","do":"tanda","peeks":["M1","M7"],"follows":["M7"],"why":"cuerpo del latch y caller del port"}
{"action":"ola_v1","do":"cerrar","why":"el control vive en pkg::run_job","huecos":["run_job_async"]}
)";
}

std::string wave_pilot_user_prompt(const WaveState& st) {
  std::ostringstream out;
  if (wave_is_last_propose(st)) {
    out << "ÚLTIMA OLA. Cierra si ya entendiste; si no, un gesto más. "
           "El runtime cierra después si hace falta.\n\n";
  } else if (wave_circuit_complete(st)) {
    out << "Circuito ON y OFF anclado (hecho, no vallado). Cierra si te basta.\n\n";
  } else if (st.wave_n <= 1 && !st.opened_ids.empty()) {
    out << "Tras cover: tanda 1 latch + 1 caller (port de la ficha). "
           "No tres peeks del mismo archivo del LED.\n\n";
  }
  out << "Elige UNA ola. No copies plantillas.\n\n";
  out << "## Consulta\n" << st.prompt << "\n\n";
  out << wave_work_markdown(st);
  return out.str();
}

bool pack_skip_id(const std::string& a) {
  if (a.size() < 2) {
    return true;
  }
  if (a[0] == 'M' && a.size() <= 4) {
    bool digits = true;
    for (std::size_t i = 1; i < a.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(a[i]))) {
        digits = false;
        break;
      }
    }
    if (digits) {
      return true;
    }
  }
  return false;
}

std::string pack_canon_anchor(const WaveState& st, const std::string& loc) {
  if (is_file_only_loc(loc)) {
    return loc;
  }
  std::string path;
  std::string symbol;
  resolve_locus(st, loc, &path, &symbol);
  if (!path.empty() && !symbol.empty()) {
    return path + ":" + symbol;
  }
  if (!symbol.empty()) {
    return symbol;
  }
  return loc;
}

void pack_push_prefer_path(std::vector<std::string>* dst, const std::string& anchor) {
  if (dst == nullptr || anchor.empty() || pack_skip_id(anchor)) {
    return;
  }
  const std::string tail = symbol_tail(anchor);
  for (auto& e : *dst) {
    if (ascii_lower(e) == ascii_lower(anchor)) {
      return;
    }
    if (!tail.empty() && tail.size() >= 4 && symbol_tail(e) == tail) {
      if (anchor.find('/') != std::string::npos && e.find('/') == std::string::npos) {
        e = anchor;
      }
      return;
    }
  }
  dst->push_back(anchor);
}

bool pack_is_read(const WaveState& st, const std::string& loc) {
  return list_has_locus(st.peeks_done, loc, st.candidatas) ||
         list_has_locus(st.follows_done, loc, st.candidatas);
}

bool pack_hueco_interesting(const std::string& loc) {
  const std::string tail = symbol_tail(loc);
  if (tail.size() < 6 || outgoing_skip_name(tail)) {
    return false;
  }
  if (name_looks_on(tail) || name_looks_off(tail)) {
    return true;
  }
  const std::string low = ascii_lower(tail);
  if (low.find("thinking") != std::string::npos || low.find("busy") != std::string::npos ||
      low.find("spinner") != std::string::npos) {
    return true;
  }
  if (low.rfind("run_", 0) == 0 || low.rfind("handle_", 0) == 0 ||
      low.rfind("begin_", 0) == 0 || low.rfind("end_", 0) == 0) {
    return true;
  }
  return false;
}

void pack_push_hueco(std::vector<std::string>* dst, const WaveState& st, const std::string& loc) {
  if (dst == nullptr || loc.empty() || pack_skip_id(loc)) {
    return;
  }
  if (loc.size() < 4) {
    return;
  }
  if (pack_is_read(st, loc)) {
    return;
  }
  const std::string canon = pack_canon_anchor(st, loc);
  if (pack_is_read(st, canon)) {
    return;
  }
  if (!pack_hueco_interesting(canon) && !pack_hueco_interesting(loc)) {
    return;
  }
  pack_push_prefer_path(dst, canon);
}

std::vector<std::string> pack_snake_idents(const std::string& text) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  const std::size_t n = text.size();
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!(std::isalpha(c) || c == '_')) {
      continue;
    }
    std::size_t j = i + 1;
    while (j < n) {
      const unsigned char d = static_cast<unsigned char>(text[j]);
      if (!(std::isalnum(d) || d == '_')) {
        break;
      }
      ++j;
    }
    std::string name = text.substr(i, j - i);
    i = j - 1;
    if (name.find('_') == std::string::npos || name.size() < 6) {
      continue;
    }
    const std::string key = ascii_lower(name);
    if (!seen.insert(key).second) {
      continue;
    }
    out.push_back(name);
  }
  return out;
}

WavePackHandoff wave_pack_handoff(const WaveState& st) {
  WavePackHandoff h;
  h.why = st.cierre;
  for (const auto& p : st.peeks_done) {
    pack_push_prefer_path(&h.visto, pack_canon_anchor(st, p));
  }
  for (const auto& f : st.follows_done) {
    pack_push_prefer_path(&h.seguido, pack_canon_anchor(st, f));
  }
  for (const auto& rec : st.needles_log) {
    if (!rec.in_locus.empty()) {
      pack_push_prefer_path(&h.in_done, pack_canon_anchor(st, rec.in_locus));
    }
  }
  auto consider = [&](const std::string& loc) { pack_push_hueco(&h.huecos, st, loc); };
  for (const auto& c : st.circuit_callers_on) {
    consider(c);
  }
  for (const auto& c : st.circuit_callers_off) {
    consider(c);
  }
  for (const auto& hit : st.candidatas) {
    if (hit.needle != "follow" && hit.needle != "entre") {
      continue;
    }
    const std::string key =
        (!hit.path.empty() && !hit.symbol.empty()) ? (hit.path + ":" + hit.symbol) : hit.symbol;
    consider(key);
  }
  for (const auto& name : pack_snake_idents(st.cierre)) {
    consider(name);
  }
  for (const auto& name : wave_extract_call_names(st.cierre)) {
    consider(name);
  }
  for (const auto& claimed : st.huecos_claimed) {
    if (claimed.empty() || pack_is_read(st, claimed)) {
      continue;
    }
    pack_push_prefer_path(&h.huecos, pack_canon_anchor(st, claimed));
  }
  constexpr int kMaxHuecos = 16;
  if (static_cast<int>(h.huecos.size()) > kMaxHuecos) {
    h.huecos.resize(static_cast<std::size_t>(kMaxHuecos));
  }
  return h;
}

nlohmann::json wave_pack_to_json(const WaveState& st) {
  const auto h = wave_pack_handoff(st);
  return {{"why", h.why},
          {"visto", h.visto},
          {"seguido", h.seguido},
          {"in_done", h.in_done},
          {"huecos", h.huecos}};
}

std::string wave_pack_markdown(const WaveState& st) {
  const auto h = wave_pack_handoff(st);
  std::ostringstream out;
  out << "## Pack\n";
  if (!h.why.empty()) {
    out << "why: " << h.why << "\n";
  }
  out << "\n### Visto (leído; empaquetar)\n";
  if (h.visto.empty()) {
    out << "(nada)\n";
  } else {
    for (const auto& a : h.visto) {
      out << "- `" << a << "`\n";
    }
  }
  out << "\n### Seguido\n";
  if (h.seguido.empty()) {
    out << "(nada)\n";
  } else {
    for (const auto& a : h.seguido) {
      out << "- `" << a << "`\n";
    }
  }
  out << "\n### in\n";
  if (h.in_done.empty()) {
    out << "(nada)\n";
  } else {
    for (const auto& a : h.in_done) {
      out << "- `" << a << "`\n";
    }
  }
  out << "\n### Huecos (no leído; L2 puede abrir)\n";
  if (h.huecos.empty()) {
    out << "(nada)\n";
  } else {
    for (const auto& a : h.huecos) {
      out << "- `" << a << "`\n";
    }
  }
  return out.str();
}

void wave_attach_cierre_caption(WaveState* st) {
  if (st == nullptr || st->cierre.empty()) {
    return;
  }
  if (st->cierre.find("Visto:") == 0) {
    return;
  }
  const auto h = wave_pack_handoff(*st);
  std::ostringstream cap;
  cap << "Visto:";
  if (h.visto.empty()) {
    cap << " (nada)";
  } else {
    for (const auto& a : h.visto) {
      cap << " `" << a << "`";
    }
  }
  cap << "\nHuecos:";
  if (h.huecos.empty()) {
    cap << " (nada)";
  } else {
    for (const auto& a : h.huecos) {
      cap << " `" << a << "`";
    }
  }
  cap << "\n(El why no es evidencia de lo no leído.)\n\n";
  st->cierre = cap.str() + st->cierre;
}

nlohmann::json wave_state_to_json(const WaveState& st) {
  nlohmann::json cands = nlohmann::json::array();
  for (const auto& h : st.candidatas) {
    cands.push_back({{"id", h.id},
                     {"path", h.path},
                     {"symbol", h.symbol},
                     {"stem", h.stem},
                     {"kind", h.kind},
                     {"needle", h.needle},
                     {"files", h.files}});
  }
  nlohmann::json needles_log = nlohmann::json::array();
  for (const auto& rec : st.needles_log) {
    needles_log.push_back({{"needle", rec.needle},
                           {"in", rec.in_locus},
                           {"hits", rec.hits},
                           {"added", rec.added},
                           {"ids", rec.ids}});
  }
  nlohmann::json olas_log = nlohmann::json::array();
  for (const auto& e : st.olas_log) {
    olas_log.push_back(
        {{"n", e.n}, {"do", e.do_name}, {"why", e.why}, {"detail", e.detail}});
  }
  nlohmann::json zonas = nlohmann::json::array();
  for (const auto& z : st.zonas) {
    zonas.push_back({{"id", z.id}, {"verdict", z.verdict}});
  }
  nlohmann::json neigh = nlohmann::json::array();
  for (const auto& n : st.peek_neighbors) {
    neigh.push_back({{"loc", n.loc}, {"callers", n.callers}, {"callees", n.callees}});
  }
  nlohmann::json sketch = nlohmann::json::array();
  for (const auto& e : wave_sketch_edges(st)) {
    sketch.push_back({{"from", e.from}, {"to", e.to}, {"via", e.via}});
  }
  return {{"prompt", st.prompt},
          {"campo", st.campo},
          {"atlas_md", st.atlas_md},
          {"opened_md", st.opened_md},
          {"opened_ids", st.opened_ids},
          {"candidatas", cands},
          {"needles_log", needles_log},
          {"olas_log", olas_log},
          {"mencionados", st.mencionados},
          {"zonas", zonas},
          {"notas", st.notas},
          {"follow_md", st.follow_md},
          {"peeks_done", st.peeks_done},
          {"follows_done", st.follows_done},
          {"entres_done", st.entres_done},
          {"last_error", st.last_error},
          {"wave_n", st.wave_n},
          {"propose_n", st.propose_n},
          {"done", st.done},
          {"cierre", st.cierre},
          {"huecos_claimed", st.huecos_claimed},
          {"pack", wave_pack_to_json(st)},
          {"circuit_on", st.circuit_on},
          {"circuit_off", st.circuit_off},
          {"circuit_on_via", st.circuit_on_via},
          {"circuit_off_via", st.circuit_off_via},
          {"circuit_callers_on", st.circuit_callers_on},
          {"circuit_callers_off", st.circuit_callers_off},
          {"circuit_entre", st.circuit_entre},
          {"peek_neighbors", neigh},
          {"sketch", sketch}};
}

nlohmann::json wave_ola_to_json(const WaveOla& ola) {
  return {{"ok", ola.ok},
          {"error", ola.error},
          {"do", wave_do_name(ola.do_kind)},
          {"campo", ola.campo},
          {"needles", ola.needles},
          {"keep", ola.keep},
          {"drop", ola.drop},
          {"peek", ola.peek},
          {"peeks", ola.peeks},
          {"follows", ola.follows},
          {"from", ola.from},
          {"to", ola.to},
          {"in", ola.in_locus},
          {"why", ola.why},
          {"huecos", ola.huecos}};
}

}  // namespace tuide
