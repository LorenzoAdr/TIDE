#include "ai/l2_wave.hpp"
#include "ai/action_json.hpp"
#include "ai/get_code_of.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tuide {

bool outgoing_skip_name(const std::string& name);

namespace {

bool locus_keys_match(const std::string& a, const std::string& b);
void sketch_push(std::vector<WaveSketchLink>* edges, const std::string& from, const std::string& to,
                 const std::string& via);
std::string symbol_tail(const std::string& loc);
bool list_has_locus(const std::vector<std::string>& done, const std::string& loc,
                    const std::vector<WaveHit>& hits);
void resolve_locus(const WaveState& st, const std::string& loc, std::string* path,
                   std::string* symbol);

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

int json_int(const nlohmann::json& j, const char* key) {
  if (!j.contains(key)) {
    return 0;
  }
  if (j[key].is_number_integer()) {
    return j[key].get<int>();
  }
  if (j[key].is_number()) {
    return static_cast<int>(j[key].get<double>());
  }
  if (j[key].is_string()) {
    try {
      return std::stoi(j[key].get<std::string>());
    } catch (...) {
      return 0;
    }
  }
  return 0;
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
  if (low.find("wake") != std::string::npos || low.find("enqueue") != std::string::npos) {
    return true;
  }
  if (low == "tr" || low == "now" || low == "seconds" || low == "milliseconds") {
    return true;
  }
  return false;
}

std::string stem_from_path(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const auto slash = path.find_last_of("/\\");
  std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = file.rfind('.');
  if (dot != std::string::npos) {
    file.resize(dot);
  }
  return ascii_lower(file);
}

std::string loc_stem(const WaveState& st, const std::string& loc) {
  if (const WaveHit* h = wave_find_hit(st.candidatas, loc)) {
    if (!h->stem.empty()) {
      return ascii_lower(h->stem);
    }
    if (!h->path.empty()) {
      return stem_from_path(h->path);
    }
  }
  if (loc.find('/') != std::string::npos) {
    std::string path;
    std::string symbol;
    split_path_symbol(loc, &path, &symbol);
    if (!path.empty()) {
      return stem_from_path(path);
    }
  }
  return {};
}

int query_token_overlap(const WaveState& st, const std::string& loc) {
  const std::string q = ascii_lower(st.prompt);
  const std::string tail = ascii_lower(symbol_tail(loc));
  int score = 0;
  std::string tok;
  auto flush = [&]() {
    if (tok.size() >= 4 && q.find(tok) != std::string::npos) {
      score += 4;
    }
    tok.clear();
  };
  for (char c : tail) {
    if (c == '_' || c == '-') {
      flush();
    } else {
      tok.push_back(c);
    }
  }
  flush();
  return score;
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

int neighbor_hop_score(const WaveState& st, const std::string& loc, const std::string& from_loc) {
  int score = neighbor_caller_score(st, loc);
  const std::string a = loc_stem(st, from_loc);
  const std::string b = loc_stem(st, loc);
  if (!a.empty() && !b.empty() && a != b) {
    score += 12;
  }
  const std::string tail = ascii_lower(symbol_tail(loc));
  if (tail.find("restart") != std::string::npos || tail.find("stop_") != std::string::npos ||
      tail.find("alive") != std::string::npos || tail.find("crash") != std::string::npos ||
      tail.find("transport") != std::string::npos || tail.find("fail") != std::string::npos ||
      tail.find("pending") != std::string::npos || tail.find("opened") != std::string::npos) {
    score += 10;
  }
  if (tail.find("wake") != std::string::npos || tail.find("enqueue") != std::string::npos ||
      tail.find("compile_commands") != std::string::npos) {
    score -= 8;
  }
  score += query_token_overlap(st, loc);
  return score;
}

void maybe_resolve_callee(WaveState* st, const WaveOps* ops, const std::string& name) {
  if (st == nullptr || name.empty() || wave_find_hit(st->candidatas, name) != nullptr) {
    return;
  }
  if (ops == nullptr || !ops->search_needle) {
    return;
  }
  const auto hits = ops->search_needle(name, "");
  std::vector<WaveHit> keep;
  for (auto h : hits) {
    if (h.symbol.empty()) {
      h.symbol = name;
    }
    if (!locus_keys_match(h.symbol, name) && !locus_keys_match(wave_hit_key(h), name)) {
      continue;
    }
    keep.push_back(std::move(h));
    if (keep.size() >= 2) {
      break;
    }
  }
  if (!keep.empty()) {
    wave_merge_hits(st, keep);
  }
}

bool neighbor_is_read(const WaveState& st, const std::string& loc) {
  return list_has_locus(st.peeks_done, loc, st.candidatas) ||
         list_has_locus(st.follows_done, loc, st.candidatas);
}

bool hop_dup(const std::vector<WavePeekHop>& have, const std::string& loc) {
  for (const auto& h : have) {
    if (locus_keys_match(h.loc, loc)) {
      return true;
    }
  }
  return false;
}

WavePeekHop make_peek_hop(const WaveState& st, const std::string& loc, int hops) {
  WavePeekHop h;
  h.loc = loc;
  h.stem = loc_stem(st, loc);
  h.hops = hops;
  return h;
}

void push_peek_hop(std::vector<WavePeekHop>* dst, WavePeekHop hop) {
  if (dst == nullptr || hop.loc.empty() || neighbor_id_skip(hop.loc)) {
    return;
  }
  if (hop_dup(*dst, hop.loc)) {
    return;
  }
  dst->push_back(std::move(hop));
}

std::string peek_rank_query(const WaveState& st) {
  std::ostringstream out;
  out << st.prompt;
  for (const auto& p : st.papeles) {
    if (!p.empty()) {
      out << " " << p;
    }
  }
  return out.str();
}

void rank_and_trim_hops(const WaveState& st, const WaveOps* ops, const std::string& from_loc,
                        std::vector<WavePeekHop>* hops, int keep_n) {
  if (hops == nullptr || hops->empty()) {
    return;
  }
  if (ops != nullptr && ops->rank_hops) {
    ops->rank_hops(peek_rank_query(st), hops);
  }
  std::sort(hops->begin(), hops->end(), [&](const WavePeekHop& a, const WavePeekHop& b) {
    const bool ac = a.cosine >= 0.f;
    const bool bc = b.cosine >= 0.f;
    if (ac != bc) {
      return ac;
    }
    if (ac && std::fabs(a.cosine - b.cosine) > 0.0001f) {
      return a.cosine > b.cosine;
    }
    const int as = neighbor_hop_score(st, a.loc, from_loc);
    const int bs = neighbor_hop_score(st, b.loc, from_loc);
    if (as != bs) {
      return as > bs;
    }
    if (a.hops != b.hops) {
      return a.hops < b.hops;
    }
    return a.loc < b.loc;
  });
  if (keep_n > 0 && static_cast<int>(hops->size()) > keep_n) {
    hops->resize(static_cast<std::size_t>(keep_n));
  }
}

void format_hop_flags(std::ostringstream& out, const WaveState& st, const std::string& from_loc,
                      const WavePeekHop& h) {
  const std::string from_st = loc_stem(st, from_loc);
  const bool other = !from_st.empty() && !h.stem.empty() && from_st != h.stem;
  const bool unread = !neighbor_is_read(st, h.loc);
  if (h.cosine >= 0.f) {
    out << " cos=" << std::fixed << std::setprecision(2) << h.cosine;
  }
  if (h.hops > 1) {
    out << " hops=" << h.hops;
  }
  if (unread || other) {
    out << " (";
    if (unread) {
      out << "no leído";
    }
    if (unread && other) {
      out << ", ";
    }
    if (other) {
      out << "otro stem";
    }
    out << ")";
  }
}

std::string format_peek_neighbors(const WaveState& st, const WavePeekNeighbors& n) {
  if (n.callers.empty() && n.callees.empty() && n.export_callers.empty()) {
    return {};
  }
  std::ostringstream out;
  if (!n.callers.empty()) {
    out << "callers:";
    for (const auto& c : n.callers) {
      out << "\n  ↑ `" << c.loc << "`";
      if (!c.stem.empty()) {
        out << " stem=" << c.stem;
      }
      format_hop_flags(out, st, n.loc, c);
    }
    out << "\n";
  }
  if (!n.callees.empty()) {
    out << "calls:";
    for (const auto& c : n.callees) {
      out << "\n  ↓ `" << c.loc << "`";
      if (!c.stem.empty()) {
        out << " stem=" << c.stem;
      }
      format_hop_flags(out, st, n.loc, c);
    }
    out << "\n";
  }
  if (!n.export_loc.empty() && !n.export_callers.empty()) {
    out << "fan-in `" << n.export_loc << "`:";
    for (const auto& c : n.export_callers) {
      out << "\n  ↑ `" << c.loc << "`";
      format_hop_flags(out, st, n.export_loc, c);
    }
    out << "\n";
  }
  return out.str();
}

void collect_causal_callers(const WaveOps& ops, const std::string& loc, const WaveState& st,
                            std::vector<WaveHit>* out) {
  if (out == nullptr || !ops.peek_causal) {
    return;
  }
  std::string path;
  std::string symbol;
  resolve_locus(st, loc, &path, &symbol);
  if (symbol.empty()) {
    symbol = symbol_of_loc(loc);
  }
  if (symbol.empty()) {
    return;
  }
  std::string md;
  std::string cerr;
  ops.peek_causal(path, symbol, "", false, &md, out, &cerr);
}

void record_peek_neighbors(WaveState* st, const std::string& loc, const std::string& body,
                           const std::vector<WaveHit>& callers, const WaveOps* ops) {
  if (st == nullptr || loc.empty()) {
    return;
  }
  WavePeekNeighbors n;
  n.loc = loc;
  std::vector<WavePeekHop> in_hops;
  for (const auto& h : callers) {
    const std::string label = neighbor_label(h);
    if (label.empty() || neighbor_id_skip(label) || locus_keys_match(label, loc)) {
      continue;
    }
    WavePeekHop hop = make_peek_hop(*st, label, 1);
    if (hop.stem.empty() && !h.stem.empty()) {
      hop.stem = ascii_lower(h.stem);
    }
    push_peek_hop(&in_hops, std::move(hop));
  }
  rank_and_trim_hops(*st, ops, loc, &in_hops, kWavePeekExpandCap);
  const int hop_depth = st->peek_hop_depth > 0 ? st->peek_hop_depth : kWavePeekHopDepth;
  if (ops != nullptr && ops->peek_causal && hop_depth >= 2) {
    int frontier_begin = 0;
    for (int d = 2; d <= hop_depth; ++d) {
      const int frontier_end = static_cast<int>(in_hops.size());
      if (frontier_begin >= frontier_end) {
        break;
      }
      for (int i = frontier_begin; i < frontier_end; ++i) {
        if (in_hops[static_cast<std::size_t>(i)].hops != d - 1) {
          continue;
        }
        std::vector<WaveHit> inc;
        collect_causal_callers(*ops, in_hops[static_cast<std::size_t>(i)].loc, *st, &inc);
        for (const auto& h : inc) {
          const std::string label = neighbor_label(h);
          if (label.empty() || neighbor_id_skip(label) || locus_keys_match(label, loc) ||
              hop_dup(in_hops, label)) {
            continue;
          }
          WavePeekHop hop = make_peek_hop(*st, label, d);
          if (hop.stem.empty() && !h.stem.empty()) {
            hop.stem = ascii_lower(h.stem);
          }
          push_peek_hop(&in_hops, std::move(hop));
        }
      }
      frontier_begin = frontier_end;
      rank_and_trim_hops(*st, ops, loc, &in_hops, kWavePeekExpandCap);
    }
  }
  rank_and_trim_hops(*st, ops, loc, &in_hops, kWavePeekNeighborMax);
  n.callers = std::move(in_hops);

  std::vector<WavePeekHop> out_hops;
  for (const auto& c : wave_follow_outgoing_calls(body)) {
    if (c.symbol.empty() || locus_keys_match(c.symbol, loc) || neighbor_call_noise(c.symbol)) {
      continue;
    }
    maybe_resolve_callee(st, ops, c.symbol);
    push_peek_hop(&out_hops, make_peek_hop(*st, c.symbol, 1));
  }
  rank_and_trim_hops(*st, ops, loc, &out_hops, kWavePeekNeighborMax);
  n.callees = std::move(out_hops);

  const std::string here_stem = loc_stem(*st, loc);
  if (ops != nullptr && ops->peek_causal) {
    for (const auto& cal : n.callees) {
      if (cal.hops != 1) {
        continue;
      }
      const std::string cal_stem = cal.stem.empty() ? loc_stem(*st, cal.loc) : cal.stem;
      if (here_stem.empty() || cal_stem.empty() || here_stem == cal_stem) {
        continue;
      }
      std::vector<WaveHit> inc;
      collect_causal_callers(*ops, cal.loc, *st, &inc);
      std::vector<WavePeekHop> fan;
      for (const auto& h : inc) {
        const std::string label = neighbor_label(h);
        if (label.empty() || neighbor_id_skip(label) || locus_keys_match(label, loc) ||
            locus_keys_match(label, cal.loc)) {
          continue;
        }
        push_peek_hop(&fan, make_peek_hop(*st, label, 2));
      }
      rank_and_trim_hops(*st, ops, cal.loc, &fan, kWaveSketchFanInMax);
      if (!fan.empty()) {
        n.export_loc = cal.loc;
        n.export_callers = std::move(fan);
      }
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
    if (locus_keys_match(e.from, from) && locus_keys_match(e.to, to)) {
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
  for (const auto& p : st.pin_loci) {
    if (locus_keys_match(p, peek) || ascii_lower(p) == want) {
      return true;
    }
    const std::string tail = ascii_lower(symbol_tail(p));
    if (!tail.empty() && tail == want) {
      return true;
    }
  }
  if (!st.pin_from.empty() &&
      (locus_keys_match(st.pin_from, peek) || ascii_lower(st.pin_from) == want ||
       ascii_lower(symbol_tail(st.pin_from)) == want)) {
    return true;
  }
  if (!st.pin_to.empty() &&
      (locus_keys_match(st.pin_to, peek) || ascii_lower(st.pin_to) == want ||
       ascii_lower(symbol_tail(st.pin_to)) == want)) {
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
      record_peek_neighbors(st, circuit_loc, body, callers, &ops);
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

void push_seed_fn(std::vector<std::pair<std::string, std::string>>* seeds, const std::string& path,
                  const std::string& symbol) {
  if (seeds == nullptr || symbol.empty()) {
    return;
  }
  const std::string sl = ascii_lower(symbol);
  const std::string pl = ascii_lower(path);
  for (const auto& e : *seeds) {
    if (ascii_lower(e.second) != sl) {
      continue;
    }
    if (path.empty() || e.first.empty() || ascii_lower(e.first) == pl) {
      return;
    }
  }
  if (static_cast<int>(seeds->size()) >= kWaveCercaMaxSeeds) {
    return;
  }
  seeds->push_back({path, symbol});
}

void push_boost_stem(std::vector<std::string>* stems, const std::string& stem) {
  if (stems == nullptr || stem.empty()) {
    return;
  }
  const std::string key = ascii_lower(stem);
  for (const auto& s : *stems) {
    if (ascii_lower(s) == key) {
      return;
    }
  }
  stems->push_back(stem);
}

void collect_cerca_seeds(const WaveState& st, const std::vector<std::string>& in_scopes,
                         std::vector<std::pair<std::string, std::string>>* seed_fns,
                         std::vector<std::string>* boost_stems) {
  if (seed_fns == nullptr || boost_stems == nullptr) {
    return;
  }
  auto add_loc = [&](const std::string& loc) {
    if (loc.empty()) {
      return;
    }
    std::string path;
    std::string symbol;
    resolve_locus(st, loc, &path, &symbol);
    if (!symbol.empty()) {
      push_seed_fn(seed_fns, path, symbol);
    }
  };
  if (!in_scopes.empty()) {
    for (const auto& sc : in_scopes) {
      if (sc.empty()) {
        continue;
      }
      if (const WaveHit* h = wave_find_hit(st.candidatas, sc)) {
        push_seed_fn(seed_fns, h->path, h->symbol);
        push_boost_stem(boost_stems, h->stem);
        continue;
      }
      std::string path;
      std::string symbol;
      split_path_symbol(sc, &path, &symbol);
      if (!symbol.empty() && sc.find('/') != std::string::npos) {
        push_seed_fn(seed_fns, path, symbol);
        continue;
      }
      push_boost_stem(boost_stems, sc);
    }
    return;
  }
  for (const auto& p : st.peeks_done) {
    add_loc(p);
  }
  for (const auto& n : st.peek_neighbors) {
    add_loc(n.loc);
    for (const auto& c : n.callees) {
      add_loc(c.loc);
    }
    for (const auto& c : n.callers) {
      add_loc(c.loc);
    }
    if (!n.export_loc.empty()) {
      add_loc(n.export_loc);
    }
    for (const auto& c : n.export_callers) {
      add_loc(c.loc);
    }
  }
  for (const auto& z : st.zonas) {
    if (z.verdict != "keep") {
      continue;
    }
    if (const WaveHit* h = wave_find_hit(st.candidatas, z.id)) {
      push_seed_fn(seed_fns, h->path, h->symbol);
      push_boost_stem(boost_stems, h->stem);
    }
  }
}

bool cerca_already_logged(const WaveState& st, const std::vector<std::string>& needles,
                          const std::vector<std::string>& in_scopes) {
  const std::string q = ascii_lower(wave_cerca_query(needles));
  auto scopes_key = [](const std::vector<std::string>& xs) {
    std::string s;
    for (const auto& x : xs) {
      if (!s.empty()) {
        s += '\t';
      }
      s += ascii_lower(x);
    }
    return s;
  };
  const std::string in_key = scopes_key(in_scopes);
  for (const auto& rec : st.cerca_log) {
    if (ascii_lower(rec.query) == q && scopes_key(rec.in_scopes) == in_key) {
      return true;
    }
  }
  return false;
}

bool needle_is_already_read_symbol(const WaveState& st, const std::string& needle) {
  if (needle.find(' ') != std::string::npos) {
    return false;
  }
  const std::string want = ascii_lower(needle);
  auto hit_sym = [&](const std::string& loc) {
    std::string path;
    std::string symbol;
    resolve_locus(st, loc, &path, &symbol);
    return ascii_lower(symbol) == want || ascii_lower(symbol_tail(loc)) == want;
  };
  for (const auto& p : st.peeks_done) {
    if (hit_sym(p)) {
      return true;
    }
  }
  for (const auto& z : st.zonas) {
    if (z.verdict != "keep") {
      continue;
    }
    if (const WaveHit* h = wave_find_hit(st.candidatas, z.id)) {
      if (ascii_lower(h->symbol) == want) {
        return true;
      }
    }
  }
  return false;
}

int clamp_cerca_hops(int hops, const WaveState& st) {
  const int cap = st.cerca_hops_max > 0 ? st.cerca_hops_max : kWaveCercaHopsMax;
  if (hops <= 0) {
    return st.cerca_hops_max > 0 ? cap : kWaveCercaHopsDefault;
  }
  if (hops > cap) {
    return cap;
  }
  return hops;
}

bool apply_cerca(WaveState* st, const WaveOla& ola, const WaveOps& ops, std::string* detail,
                 std::string* err) {
  if (!ops.search_cerca) {
    if (err) {
      *err = "sin buscador cerca";
    }
    return false;
  }
  std::vector<std::pair<std::string, std::string>> seed_fns;
  std::vector<std::string> boost_stems;
  collect_cerca_seeds(*st, ola.in_scopes, &seed_fns, &boost_stems);
  const int hops = clamp_cerca_hops(ola.hops, *st);
  const std::string query = wave_cerca_query(ola.needles);
  std::vector<WaveCercaHit> rows;
  std::string qerr;
  std::string wake_note;
  if (!ops.search_cerca(query, seed_fns, boost_stems, ola.in_scopes, hops, &rows, &qerr,
                        &wake_note)) {
    if (err) {
      *err = qerr.empty() ? "cerca falló" : qerr;
    }
    return false;
  }
  if (static_cast<int>(rows.size()) > kWaveCercaMaxHits) {
    rows.resize(static_cast<std::size_t>(kWaveCercaMaxHits));
  }
  WaveCercaLog rec;
  rec.needles = ola.needles;
  rec.in_scopes = ola.in_scopes;
  rec.query = query;
  rec.hops = hops;
  rec.hits = static_cast<int>(rows.size());
  rec.wake_note = wake_note;
  std::vector<WaveHit> batch;
  for (auto& row : rows) {
    row.already_read = list_has_locus(st->peeks_done, row.symbol, st->candidatas) ||
                       (!row.path.empty() && !row.symbol.empty() &&
                        list_has_locus(st->peeks_done, row.path + ":" + row.symbol, st->candidatas));
    WaveHit w;
    w.id = row.id.empty() ? (row.path.empty() ? row.symbol : row.path + ":" + row.symbol) : row.id;
    w.path = row.path;
    w.symbol = row.symbol;
    w.stem = row.stem;
    w.kind = row.kind.empty() ? "fn" : row.kind;
    w.needle = "cerca";
    if (!w.path.empty()) {
      add_path_and_sibling(&w.files, w.path);
    }
    batch.push_back(std::move(w));
    rec.rows.push_back(row);
  }
  const auto n0 = st->candidatas.size();
  wave_merge_hits(st, batch);
  rec.added = static_cast<int>(st->candidatas.size() - n0);
  std::ostringstream det;
  det << "hits=" << rec.hits << " +" << rec.added << " hops=" << hops;
  if (!rec.wake_note.empty()) {
    det << " " << rec.wake_note;
  }
  if (rec.hits == 0) {
    det << " (ausencia en el barrio)";
  } else {
    int shown = 0;
    for (const auto& row : rec.rows) {
      if (shown >= 4) {
        break;
      }
      det << "; ";
      if (!row.symbol.empty()) {
        det << row.symbol;
      } else {
        det << row.id;
      }
      det << "@h" << row.hop;
      ++shown;
    }
  }
  if (detail != nullptr) {
    *detail = det.str();
  }
  st->cerca_log.push_back(std::move(rec));
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

bool wave_cerca_concept_ok(const std::string& needle) {
  std::size_t a = 0;
  std::size_t b = needle.size();
  while (a < b && std::isspace(static_cast<unsigned char>(needle[a])) != 0) {
    ++a;
  }
  while (b > a && std::isspace(static_cast<unsigned char>(needle[b - 1])) != 0) {
    --b;
  }
  if (b - a < 2 || b - a > 48) {
    return false;
  }
  int words = 0;
  bool in_word = false;
  for (std::size_t i = a; i < b; ++i) {
    const unsigned char c = static_cast<unsigned char>(needle[i]);
    if (c == '/' || c == ':' || c == '?' || c == '!' || c == ';' || c == ',') {
      return false;
    }
    if (c == '.') {
      return false;
    }
    if (std::isspace(c) != 0) {
      if (in_word) {
        ++words;
        in_word = false;
      }
    } else {
      in_word = true;
    }
  }
  if (in_word) {
    ++words;
  }
  return words >= 1 && words <= 3;
}

std::string wave_cerca_clip_concept(const std::string& needle) {
  std::size_t a = 0;
  std::size_t b = needle.size();
  while (a < b && std::isspace(static_cast<unsigned char>(needle[a])) != 0) {
    ++a;
  }
  while (b > a && std::isspace(static_cast<unsigned char>(needle[b - 1])) != 0) {
    --b;
  }
  if (a >= b) {
    return {};
  }
  for (std::size_t i = a; i < b; ++i) {
    const unsigned char c = static_cast<unsigned char>(needle[i]);
    if (c == '/' || c == ':' || c == '`' || c == '?' || c == '!' || c == ';' || c == ',' ||
        c == '.') {
      return {};
    }
  }
  std::vector<std::string> words;
  std::string cur;
  for (std::size_t i = a; i < b; ++i) {
    const unsigned char c = static_cast<unsigned char>(needle[i]);
    if (std::isspace(c) != 0) {
      if (!cur.empty()) {
        words.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(static_cast<char>(c));
    }
  }
  if (!cur.empty()) {
    words.push_back(cur);
  }
  if (words.empty()) {
    return {};
  }
  auto join = [&](std::size_t from, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && from + i < words.size(); ++i) {
      if (!s.empty()) {
        s += ' ';
      }
      s += words[from + i];
    }
    return s;
  };
  if (words.size() <= 3) {
    const std::string s = join(0, words.size());
    return wave_cerca_concept_ok(s) ? s : std::string{};
  }
  const std::string tail = join(words.size() - 3, 3);
  if (wave_cerca_concept_ok(tail)) {
    return tail;
  }
  const std::string head = join(0, 3);
  return wave_cerca_concept_ok(head) ? head : std::string{};
}

void wave_control_sanitize_hacia(std::vector<std::string>* hacia) {
  if (hacia == nullptr || hacia->empty()) {
    return;
  }
  const std::string c = wave_cerca_clip_concept(hacia->front());
  if (c.empty()) {
    hacia->clear();
  } else {
    *hacia = {c};
  }
}

std::string wave_cerca_query(const std::vector<std::string>& needles) {
  std::string q;
  for (const auto& n : needles) {
    std::size_t a = 0;
    std::size_t b = n.size();
    while (a < b && std::isspace(static_cast<unsigned char>(n[a])) != 0) {
      ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(n[b - 1])) != 0) {
      --b;
    }
    if (a >= b) {
      continue;
    }
    if (!q.empty()) {
      q += ' ';
    }
    q.append(n, a, b - a);
  }
  return q;
}

bool wave_cerca_needles_ok(const std::vector<std::string>& needles, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (static_cast<int>(needles.size()) < kWaveCercaMinNeedles) {
    return set("cerca sin needles");
  }
  if (static_cast<int>(needles.size()) > kWaveCercaMaxNeedles) {
    return set("cerca: máximo 6 conceptos");
  }
  for (const auto& n : needles) {
    if (!wave_cerca_concept_ok(n)) {
      return set("cerca: cada needle es un concepto de 1–3 palabras, no frase ni id");
    }
  }
  return true;
}

namespace {

std::string trim_ws_copy(const std::string& s) {
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
    ++a;
  }
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
    --b;
  }
  return s.substr(a, b - a);
}

bool wave_log_has_do(const WaveState& st, const char* name) {
  for (const auto& e : st.olas_log) {
    if (e.do_name == name) {
      return true;
    }
  }
  return false;
}

bool papel_token_is_zone_id(const std::string& t) {
  if (t.size() < 2 || t.size() > 4) {
    return false;
  }
  if (t[0] != 'M' && t[0] != 'm') {
    return false;
  }
  for (std::size_t i = 1; i < t.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(t[i])) == 0) {
      return false;
    }
  }
  return true;
}

bool guion_stopword(const std::string& t) {
  static const char* kStop[] = {
      "a",     "al",    "como",  "con",   "cual",  "cuando", "de",    "del",  "el",
      "en",    "es",    "esta",  "este",  "hay",   "la",    "las",   "lo",   "los",
      "mas",   "más",   "no",    "o",     "para",  "por",   "que",   "qué",  "se",
      "si",    "sí",    "sin",   "su",    "sus",   "the",   "un",    "una",  "unas",
      "uno",   "unos",  "y",     "ya",    "quien", "quién", "como",  "cómo", "donde",
      "dónde", "cual",  "cuál"};
  const std::string low = ascii_lower(t);
  for (const char* s : kStop) {
    if (low == s || t == s) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> papel_words(const std::string& papel) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.empty()) {
      return;
    }
    out.push_back(cur);
    cur.clear();
  };
  for (unsigned char c : papel) {
    if (std::isspace(c) != 0 || c == '?' || c == '!' || c == ',' || c == ';' || c == '"') {
      flush();
    } else {
      cur.push_back(static_cast<char>(c));
    }
  }
  flush();
  return out;
}

std::string guion_evidence_blob(const WaveState& st) {
  std::string b = st.notas;
  b.push_back('\n');
  b += st.follow_md;
  for (const auto& p : st.peeks_done) {
    b.push_back(' ');
    b += p;
  }
  for (const auto& p : st.follows_done) {
    b.push_back(' ');
    b += p;
  }
  for (const auto& rec : st.needles_log) {
    b.push_back(' ');
    b += rec.needle;
    b.push_back(' ');
    b += rec.in_locus;
  }
  for (const auto& rec : st.cerca_log) {
    b.push_back(' ');
    b += rec.query;
    for (const auto& n : rec.needles) {
      b.push_back(' ');
      b += n;
    }
    for (const auto& sc : rec.in_scopes) {
      b.push_back(' ');
      b += sc;
    }
  }
  return ascii_lower(b);
}

}  // namespace

bool wave_guion_papel_ok(const std::string& papel) {
  const std::string t = trim_ws_copy(papel);
  if (t.size() < 2 || t.size() > static_cast<std::size_t>(kWaveGuionPapelChars)) {
    return false;
  }
  for (unsigned char c : t) {
    if (c == '/' || c == ':' || c == '.' || c == '_') {
      return false;
    }
  }
  const auto words = papel_words(t);
  if (static_cast<int>(words.size()) < kWaveGuionWordsMin ||
      static_cast<int>(words.size()) > kWaveGuionWordsMax) {
    return false;
  }
  for (const auto& w : words) {
    if (papel_token_is_zone_id(w)) {
      return false;
    }
  }
  return true;
}

bool wave_guion_papeles_ok(const std::vector<std::string>& papeles, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (static_cast<int>(papeles.size()) < kWaveGuionMin) {
    return set("guion: 2–6 papeles");
  }
  if (static_cast<int>(papeles.size()) > kWaveGuionMax) {
    return set("guion: 2–6 papeles");
  }
  for (const auto& p : papeles) {
    if (!wave_guion_papel_ok(p)) {
      return set("guion: papel sin path, id, stem ni M*");
    }
  }
  return true;
}

std::vector<std::string> wave_guion_uncovered(const WaveState& st) {
  std::vector<std::string> miss;
  if (st.papeles.empty()) {
    return miss;
  }
  const std::string blob = guion_evidence_blob(st);
  for (const auto& papel : st.papeles) {
    const auto words = papel_words(trim_ws_copy(papel));
    std::vector<std::string> content;
    for (const auto& w : words) {
      if (w.size() < 3 || guion_stopword(w)) {
        continue;
      }
      content.push_back(ascii_lower(w));
    }
    bool hit = false;
    if (content.empty()) {
      const std::string whole = ascii_lower(trim_ws_copy(papel));
      hit = !whole.empty() && blob.find(whole) != std::string::npos;
    } else {
      for (const auto& tok : content) {
        if (blob.find(tok) != std::string::npos) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) {
      miss.push_back(papel);
    }
  }
  return miss;
}

std::string wave_strategy_markdown() {
  return R"(## Estrategia
1. Localiza el sistema del que habla la consulta (el objeto).
2. Luego el verbo derivado sobre ese sistema.
3. Con el sistema anclado: disparo y efecto son dos ramas distintas.
El Guion es el examen. No es el plan de olas.
)";
}

std::string wave_guion_markdown(const WaveState& st) {
  std::ostringstream out;
  out << "## Guion\n";
  out << "(examen: qué tendrías que poder explicar; no es el orden de exploración)\n";
  if (st.papeles.empty()) {
    out << "(aún no hay preguntas)\n";
    return out.str();
  }
  const auto miss = wave_guion_uncovered(st);
  for (std::size_t i = 0; i < st.papeles.size(); ++i) {
    const auto& p = st.papeles[i];
    const bool abierta = std::find(miss.begin(), miss.end(), p) != miss.end();
    out << (i + 1) << ". " << p << (abierta ? "  [sin evidencia]" : "  [con evidencia]") << "\n";
  }
  return out.str();
}

bool wave_independiente_ok(const WaveState& st, int papel, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (st.control_worker) {
    return set("explorador no orquesta");
  }
  if (st.independiente_leaf) {
    return set("independiente no anida");
  }
  if (papel < 1 || papel > static_cast<int>(st.papeles.size())) {
    return set("independiente: papel 1-based del guion");
  }
  if (!wave_log_has_do(st, "juicio")) {
    return set("independiente: primero cover y un peek o cerca");
  }
  if (st.peeks_done.empty() && st.cerca_log.empty()) {
    return set("independiente: primero cover y un peek o cerca");
  }
  if (wave_is_last_propose(st)) {
    return set("independiente: no en la última ola");
  }
  for (int d : st.independiente_done) {
    if (d == papel) {
      return set("independiente: ese papel ya se lanzó");
    }
  }
  const std::string& want = st.papeles[static_cast<std::size_t>(papel - 1)];
  const auto miss = wave_guion_uncovered(st);
  bool abierta = false;
  for (const auto& p : miss) {
    if (p == want) {
      abierta = true;
      break;
    }
  }
  if (!abierta) {
    return set("independiente: esa pregunta ya tiene evidencia en lo anclado");
  }
  return true;
}

std::vector<int> wave_independiente_open(const WaveState& st) {
  std::vector<int> out;
  for (int i = 1; i <= static_cast<int>(st.papeles.size()); ++i) {
    if (wave_independiente_ok(st, i, nullptr)) {
      out.push_back(i);
    }
  }
  return out;
}

std::string wave_independiente_cue_markdown(const WaveState& st) {
  const auto open = wave_independiente_open(st);
  if (open.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "Papeles [sin evidencia] que no salen de lo anclado:";
  for (std::size_t i = 0; i < open.size(); ++i) {
    out << (i == 0 ? " " : ", ") << open[i];
  }
  out << ". independiente cabe (papel=N).\n";
  return out.str();
}

WaveState wave_independiente_child(const WaveState& parent, const std::string& prompt) {
  WaveState child;
  child.prompt = prompt;
  child.papeles = {prompt};
  child.independiente_leaf = true;
  child.atlas_md = parent.atlas_md;
  child.atlas_cards = parent.atlas_cards;
  child.atlas_seed = parent.atlas_seed;
  wave_merge_hits(&child, parent.atlas_seed);
  return child;
}

void wave_merge_independiente(WaveState* parent, const WaveState& child, int papel) {
  if (parent == nullptr) {
    return;
  }
  const auto h = wave_pack_handoff(child);
  std::ostringstream block;
  block << "\n### independiente `" << papel << "`\n";
  block << "consulta: " << child.prompt << "\n";
  block << "### Visto (hijo)\n";
  if (h.visto.empty()) {
    block << "(nada)\n";
  } else {
    for (const auto& a : h.visto) {
      block << "- `" << a << "`\n";
    }
  }
  block << "### Huecos (hijo)\n";
  if (h.huecos.empty()) {
    block << "(nada)\n";
  } else {
    for (const auto& a : h.huecos) {
      block << "- `" << a << "`\n";
    }
  }
  if (!child.notas.empty()) {
    block << child.notas;
    if (child.notas.back() != '\n') {
      block << "\n";
    }
  }
  std::string add = block.str();
  constexpr std::size_t kCap = 12000;
  if (add.size() > kCap) {
    add.resize(kCap);
    add += "\n…\n";
  }
  parent->notas += add;
  if (!child.follow_md.empty()) {
    if (!parent->follow_md.empty()) {
      parent->follow_md += "\n";
    }
    parent->follow_md += child.follow_md;
  }
  std::unordered_set<std::string> kept;
  for (const auto& id : child.opened_ids) {
    kept.insert(ascii_lower(id));
  }
  std::vector<WaveHit> bring;
  for (const auto& h : child.candidatas) {
    if (h.needle == "atlas") {
      if (kept.empty() || (!kept.count(ascii_lower(h.id)) &&
                           !kept.count(ascii_lower(wave_hit_key(h))))) {
        continue;
      }
    }
    bring.push_back(h);
  }
  wave_merge_hits(parent, bring);
  for (const auto& p : child.peeks_done) {
    if (!list_has_locus(parent->peeks_done, p, parent->candidatas)) {
      parent->peeks_done.push_back(p);
    }
  }
  for (const auto& f : child.follows_done) {
    if (!list_has_locus(parent->follows_done, f, parent->candidatas)) {
      parent->follows_done.push_back(f);
    }
  }
  parent->independiente_done.push_back(papel);
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
  if (st->atlas_seed.empty()) {
    st->atlas_seed = hits;
  }
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

bool wave_needs_guion(const WaveState& st) {
  if (st.control_worker) {
    return false;
  }
  return st.papeles.empty() && !st.done;
}

bool wave_needs_cover(const WaveState& st) {
  if (st.done || st.atlas_md.empty() || wave_log_has_do(st, "juicio")) {
    return false;
  }
  if (!st.control_worker && st.papeles.empty()) {
    return false;
  }
  return true;
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
  if (audit.do_kind == WaveDo::Independiente) {
    return wave_independiente_ok(st, audit.papel, nullptr);
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

std::string wave_close_audit_instructions(const WaveState& st) {
  const auto open = wave_independiente_open(st);
  const auto miss = wave_guion_uncovered(st);
  std::ostringstream out;
  out << "¿El why está cubierto por Peeks/Follows, o falta el flujo de un locus anclado?\n";
  out << "Completo → do=cerrar (why honesto; huecos[] = nombres afirmados y no leídos).\n";
  out << "Falta UN hueco nombrado en el why y no leído → un peek de ESE locus.\n";
  out << "Falta el flujo de un locus ya anclado (Peeks / Circuito / callers ON-OFF) "
         "o nombrado en el why → UN follow de ESE locus (1; 2 solo si son callers del Circuito).\n";
  if (!open.empty()) {
    out << "Un papel [sin evidencia] que no sale de lo anclado → do=independiente con ese "
           "índice (1-based). El runtime usa ESA frase; no redactes un prompt.\n";
  }
  out << "PROHIBIDO needles, in de archivo, tanda, juicio, peek de algo no nombrado.\n";
  if (open.empty()) {
    out << "Si dudas, do=cerrar. Un gesto inválido se descarta y se aplica el borrador.\n";
  } else {
    out << "Un gesto inválido se descarta y se aplica el borrador.\n";
  }
  if (!miss.empty()) {
    out << "Preguntas del guion sin peek/cerca:";
    for (const auto& p : miss) {
      out << " `" << p << "`";
    }
    out << ".\nAfirmarlos en el why sin leerlos no cuenta.\n";
    if (open.empty()) {
      out << "Si falta uno y no tiene nombre, do=cerrar (el piloto ya tuvo olas); no inventes "
             "un peek.\n";
    } else {
      out << "No inventes un peek para un papel sin locus en lo anclado.\n";
    }
  }
  return out.str();
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

WaveOla wave_salvage_guion(const std::string& raw) {
  WaveOla ola = wave_parse_ola(raw);
  if (ola.ok && ola.do_kind == WaveDo::Guion) {
    return ola;
  }
  const auto key = raw.rfind("\"papeles\"");
  if (key == std::string::npos) {
    return ola;
  }
  const auto bra = raw.find('[', key);
  if (bra == std::string::npos) {
    return ola;
  }
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  std::size_t end = std::string::npos;
  for (std::size_t i = bra; i < raw.size(); ++i) {
    const char c = raw[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
      if (depth == 0) {
        end = i;
        break;
      }
    }
  }
  if (end == std::string::npos) {
    return ola;
  }
  nlohmann::json arr;
  try {
    arr = nlohmann::json::parse(raw.substr(bra, end - bra + 1));
  } catch (...) {
    return ola;
  }
  if (!arr.is_array()) {
    return ola;
  }
  std::vector<std::string> papeles;
  for (const auto& item : arr) {
    if (!item.is_string()) {
      continue;
    }
    const std::string p = item.get<std::string>();
    if (wave_guion_papel_ok(p)) {
      papeles.push_back(p);
    }
  }
  if (!wave_guion_papeles_ok(papeles, nullptr)) {
    return ola;
  }
  WaveOla g;
  g.ok = true;
  g.do_kind = WaveDo::Guion;
  g.papeles = std::move(papeles);
  return g;
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
  out.papeles = json_str_array(j, "papeles");
  const std::string action = j.value("action", "");
  if (action.size() < 5 || action.compare(0, 5, "ola_v") != 0) {
    if (wave_guion_papeles_ok(out.papeles, nullptr)) {
      out.do_kind = WaveDo::Guion;
      out.ok = true;
      return out;
    }
    out.error = "contrato ola_v1 inválido";
    return out;
  }
  out.do_kind = wave_do_parse(json_str(j, "do"));
  if (out.do_kind == WaveDo::Invalid) {
    if (wave_guion_papeles_ok(out.papeles, nullptr)) {
      out.do_kind = WaveDo::Guion;
    } else {
      out.error = "do inválido";
      return out;
    }
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
  if (j.contains("in") && j["in"].is_array()) {
    out.in_scopes = json_str_array(j, "in");
  } else if (out.do_kind == WaveDo::Cerca) {
    const std::string in_s = json_str(j, "in");
    if (!in_s.empty()) {
      out.in_scopes.push_back(in_s);
    }
  } else {
    out.in_locus = json_str(j, "in");
  }
  if (j.contains("hops") && j["hops"].is_number()) {
    out.hops = j["hops"].get<int>();
  }
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
  if (out.papeles.empty()) {
    out.papeles = json_str_array(j, "papeles");
  }
  out.papel = json_int(j, "papel");
  if (out.do_kind == WaveDo::Guion) {
    std::string gerr;
    if (!wave_guion_papeles_ok(out.papeles, &gerr)) {
      out.error = gerr;
      return out;
    }
  } else if (out.why.size() < 8) {
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
  if (out.do_kind == WaveDo::Cerca) {
    if (static_cast<int>(out.needles.size()) > kWaveCercaMaxNeedles) {
      out.needles.resize(static_cast<size_t>(kWaveCercaMaxNeedles));
    }
    std::string cerr;
    if (!wave_cerca_needles_ok(out.needles, &cerr)) {
      out.error = cerr;
      return out;
    }
  } else if (static_cast<int>(out.needles.size()) > kWaveMaxNeedles) {
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
  if (out.do_kind == WaveDo::Independiente && out.papel < 1) {
    out.error = "independiente: papel 1-based del guion";
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
  if (!st.control_worker && st.papeles.empty() && !st.atlas_md.empty() &&
      ola.do_kind != WaveDo::Guion) {
    return set("primero el guion (papeles de la consulta)");
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
  if (ola.do_kind == WaveDo::Cerca) {
    if (!wave_cerca_needles_ok(ola.needles, err)) {
      return false;
    }
    for (const auto& n : ola.needles) {
      if (needle_is_already_read_symbol(st, n)) {
        return set("cerca: no re-uses el símbolo ya leído");
      }
    }
    if (cerca_already_logged(st, ola.needles, ola.in_scopes)) {
      return set("cerca ya tirada");
    }
    std::vector<std::pair<std::string, std::string>> seed_fns;
    std::vector<std::string> boost_stems;
    collect_cerca_seeds(st, ola.in_scopes, &seed_fns, &boost_stems);
    if (seed_fns.empty() && boost_stems.empty()) {
      return set("cerca sin barrio (peek, keep o in)");
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Guion) {
    if (!st.papeles.empty()) {
      return set("guion ya tirado");
    }
    if (!wave_guion_papeles_ok(ola.papeles, err)) {
      return false;
    }
    return true;
  }
  if (ola.do_kind == WaveDo::Independiente) {
    return wave_independiente_ok(st, ola.papel, err);
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
  if (ola.do_kind == WaveDo::Cerca) {
    std::string det;
    std::string cerr;
    if (!apply_cerca(st, ola, ops, &det, &cerr)) {
      st->last_error = cerr;
      if (err) {
        *err = cerr;
      }
      return false;
    }
    push_ola_log(st, "cerca", ola.why, det);
    return true;
  }
  if (ola.do_kind == WaveDo::Guion) {
    st->papeles = ola.papeles;
    std::string det;
    for (const auto& p : ola.papeles) {
      if (!det.empty()) {
        det += "; ";
      }
      det += p;
    }
    push_ola_log(st, "guion", ola.why, det);
    return true;
  }
  if (ola.do_kind == WaveDo::Independiente) {
    if (!ops.run_independiente) {
      st->last_error = "independiente sin runtime";
      if (err) {
        *err = st->last_error;
      }
      return false;
    }
    const int idx = ola.papel;
    const std::string child_prompt = st->papeles[static_cast<std::size_t>(idx - 1)];
    WaveState child = wave_independiente_child(*st, child_prompt);
    std::string cerr;
    if (ops.rebuild_atlas) {
      if (!ops.rebuild_atlas(child_prompt, &child, &cerr)) {
        st->last_error = cerr.empty() ? "independiente: no se regeneró el atlas" : cerr;
        if (err) {
          *err = st->last_error;
        }
        return false;
      }
    }
    if (!ops.run_independiente(child_prompt, &child, &cerr)) {
      st->last_error = cerr.empty() ? "independiente: el hijo no arrancó" : cerr;
      if (err) {
        *err = st->last_error;
      }
      return false;
    }
    wave_merge_independiente(st, child, idx);
    push_ola_log(st, "independiente", ola.why, child_prompt);
    return true;
  }
  if (ola.do_kind != WaveDo::Cerrar) {
    st->last_error = "do inválido";
    if (err) {
      *err = st->last_error;
    }
    return false;
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
      if (lit(c.loc)) {
        sketch_push(&edges, here, node(c.loc), "call");
      }
    }
    for (const auto& c : n.callers) {
      if (lit(c.loc)) {
        sketch_push(&edges, node(c.loc), here, "call");
      }
    }
  }
  for (const auto& n : st.peek_neighbors) {
    const std::string here = node(n.loc);
    if (here.empty() || !lit(n.loc)) {
      continue;
    }
    const std::string here_stem = loc_stem(st, n.loc);
    auto interesting = [&](const WavePeekHop& h) {
      if (h.cosine >= 0.45f) {
        return true;
      }
      const std::string hs = h.stem.empty() ? loc_stem(st, h.loc) : h.stem;
      if (!here_stem.empty() && !hs.empty() && here_stem != hs) {
        return true;
      }
      return neighbor_hop_score(st, h.loc, n.loc) >= 8;
    };
    int out_rays = 0;
    for (const auto& c : n.callees) {
      if (lit(c.loc) || node(c.loc).empty()) {
        continue;
      }
      if (!interesting(c)) {
        continue;
      }
      if (out_rays >= kWaveSketchRaysPerPeek) {
        break;
      }
      sketch_push(&edges, here, node(c.loc), "ray");
      ++out_rays;
    }
    int in_rays = 0;
    for (const auto& c : n.callers) {
      if (lit(c.loc) || node(c.loc).empty()) {
        continue;
      }
      if (!interesting(c)) {
        continue;
      }
      if (in_rays >= kWaveSketchRaysPerPeek) {
        break;
      }
      sketch_push(&edges, node(c.loc), here, "ray");
      ++in_rays;
    }
    if (!n.export_loc.empty()) {
      const std::string dest = node(n.export_loc);
      if (!dest.empty()) {
        for (const auto& f : n.export_callers) {
          if (locus_keys_match(f.loc, n.loc) || node(f.loc).empty()) {
            continue;
          }
          sketch_push(&edges, node(f.loc), dest, "ray");
        }
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
  if (lit_nodes.empty()) {
    return {};
  }
  const auto edges = wave_sketch_edges(st);
  if (edges.empty() && lit_nodes.size() < 2) {
    return {};
  }
  std::ostringstream out;
  out << "bosquejo (sólido = leído; rayo = no leído):\n";
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
    const std::string nn = node(c).empty() ? c : node(c);
    for (const auto& e : edges) {
      if (locus_keys_match(e.from, nn) || locus_keys_match(e.to, nn)) {
        return;
      }
    }
    for (const auto& have : huecos) {
      if (locus_keys_match(have, nn)) {
        return;
      }
    }
    if (static_cast<int>(huecos.size()) < 3) {
      huecos.push_back(nn);
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
  }
  out << "\n" << wave_strategy_markdown();
  out << "\n" << wave_guion_markdown(st);
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
  out << "\n## Cerca (" << st.cerca_log.size() << ")\n";
  if (st.cerca_log.empty()) {
    out << "(ninguna)\n";
  } else {
    for (const auto& rec : st.cerca_log) {
      out << "- q=`" << rec.query << "` hops=" << rec.hops << " hits=" << rec.hits << " +"
          << rec.added;
      if (!rec.in_scopes.empty()) {
        out << " in";
        for (const auto& sc : rec.in_scopes) {
          out << " `" << sc << "`";
        }
      }
      if (!rec.wake_note.empty()) {
        out << "  " << rec.wake_note;
      }
      if (rec.hits == 0) {
        out << "  (ausencia en el barrio)";
      }
      out << "\n";
      for (const auto& row : rec.rows) {
        out << "    " << std::fixed << std::setprecision(2) << row.cosine << " h" << row.hop
            << " ";
        if (!row.path.empty() && !row.symbol.empty()) {
          out << row.path << ":" << row.symbol;
        } else if (!row.symbol.empty()) {
          out << row.symbol;
        } else {
          out << row.id;
        }
        if (row.already_read) {
          out << " [leído]";
        }
        if (!row.card.empty()) {
          out << "  " << row.card;
        }
        out << "\n";
      }
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
  out << "\n" << wave_strategy_markdown();
  out << "\n" << wave_guion_markdown(st);
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
  out << "\n## Cerca (" << st.cerca_log.size() << ")\n";
  if (st.cerca_log.empty()) {
    out << "(ninguna)\n";
  } else {
    for (const auto& rec : st.cerca_log) {
      out << "- q=`" << rec.query << "` hops=" << rec.hops << " hits=" << rec.hits;
      if (!rec.wake_note.empty()) {
        out << " " << rec.wake_note;
      }
      out << "\n";
      for (const auto& row : rec.rows) {
        out << "    " << row.symbol << " h" << row.hop << " " << row.cosine << "\n";
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

std::string wave_guion_system_prompt() {
  return R"(Descompón ESTA consulta en 2–6 preguntas pequeñas cuya respuesta haría falta para decir que entendiste el problema.
Cada una es un aspecto distinto de la consulta, no un paso. Son el examen, no el orden de exploración.
Si dos partes del prompt pueden ser verdad por separado, son dos preguntas.
PROHIBIDO M*, stems, paths, ids.
PROHIBIDO recitar atlas, traducir la consulta en un párrafo o explicar.
PROHIBIDO rellenar papeles copiando un molde. Cada string sale de ESTA consulta.
Puedes pensar. Al terminar, un solo JSON con action ola_v1, do guion, y papeles (array de 2–6 strings de ESTA consulta).
)";
}

std::string wave_guion_user_prompt(const WaveState& st) {
  std::ostringstream out;
  out << "Consulta:\n" << st.prompt << "\n\n";
  out << "Preguntas-examen cuya respuesta haría falta para decir que entendiste ESTA consulta. No un plan de olas. JSON al cerrar.\n";
  return out.str();
}

std::string wave_cover_system_prompt() {
  return R"(Elige 1–2 ids M* cuyo owns/nucleus sea el sistema de la consulta (el objeto).
El guion es el examen: no cubras cada pregunta. Keep vacío no vale.
Un gesto sin zona propia no vacía el keep: primero el objeto.
No elijas una zona solo porque un token de la consulta aparece en el peek.
Si el cue dice que una caza anterior falló o hay evita, no hagas keep de eso.
PROHIBIDO inventar ids.
PROHIBIDO prosa, recap, traducir el atlas o repetir estas reglas.

El primer carácter de la respuesta es `{`. Nada antes. Un solo JSON:
{"action":"ola_v1","do":"juicio","keep":["M1","M7"],"drop":["M3"],"why":"estas zonas son el sistema de la consulta"}
)";
}

std::string wave_pin_cue_markdown(const WaveState& st) {
  if (st.pin_from.empty() && st.pin_to.empty() && st.pin_loci.empty() && st.pin_hacia.empty() &&
      st.pin_evita.empty() && st.pin_fallo_md.empty()) {
    return {};
  }
  std::ostringstream out;
  if (!st.pin_fallo_md.empty()) {
    out << st.pin_fallo_md;
    if (st.pin_fallo_md.back() != '\n') {
      out << "\n";
    }
  }
  if (!st.pin_evita.empty()) {
    out << "Veto del piloto (no reabrir):";
    for (const auto& e : st.pin_evita) {
      out << " " << e;
    }
    out << "\n";
  }
  if (!st.pin_from.empty() && !st.pin_to.empty()) {
    out << "Anclas de puente (ya leídas). El objeto es el camino entre ellas. "
           "entre from/to; sin camino también es evidencia.\n";
    out << "from: " << st.pin_from << "\n";
    out << "to: " << st.pin_to << "\n";
  } else if (!st.pin_from.empty()) {
    out << "Locus semilla (ya leído). Sigue el flujo desde ahí (follow / cerca).\n";
    out << "from: " << st.pin_from << "\n";
  }
  if (!st.pin_hacia.empty()) {
    out << "Prioridad semántica (cerca):";
    for (const auto& h : st.pin_hacia) {
      out << " " << h;
    }
    out << "\n";
  }
  if (st.cerca_hops_max > 0) {
    out << "cerca hops hasta " << st.cerca_hops_max << " (no el tope 1–2).\n";
  }
  if (!st.pin_loci.empty()) {
    if (st.pin_from.empty() && st.pin_to.empty()) {
      out << "Ya leídos en un trabajo anterior. PROHIBIDO volver a peekearlos. "
             "Valen para follow/entre. El objeto es SOLO la consulta de arriba, "
             "no un recap de lo leído.\n";
    }
    out << "Loci pin (peekables):";
    for (const auto& loc : st.pin_loci) {
      out << " " << loc;
    }
    out << "\n";
  }
  out << "\n";
  return out.str();
}

std::string wave_cover_user_prompt(const WaveState& st) {
  std::ostringstream out;
  out << "Consulta:\n" << st.prompt << "\n\n";
  out << wave_pin_cue_markdown(st);
  out << wave_strategy_markdown() << "\n";
  if (!st.control_worker) {
    out << wave_guion_markdown(st) << "\n";
  }
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
El Guion es el examen: permanece; no lo reescribas ni lo sustituyas por el why.
La Estrategia es el orden de comprensión: primero el sistema, luego el verbo, luego disparo y efecto por separado. No mezcles ramas. No es un plan congelado de gestos.
Cover keep localiza el sistema, no cada pregunta.
Afirmar una pregunta o un símbolo sin haberlo leído es el mismo delito que citar un id inventado.

do:
- needles: agujas de MECANISMO (símbolos, APIs), no sinónimos del prompt. El runtime busca substring en id/path/symbol/stem. `stem::simbolo` busca el símbolo recortado a ese stem.
- cerca: `needles` son conceptos de 1–3 palabras, NO identificadores y NO una frase. El runtime embebe solo esos términos (el why no entra) y rankea fichas en el barrio: peeks + keep + rayos, o `in` (M*, path:fn, stem, prefijo). Un `in` de directorio o .cpp despierta el grafo ahí (rank barato del inventario; archivo entero → fichas de impacto). hops=1 (tope 2). hits=0 SÍ es ausencia en ese barrio. hits=0 de needles-grep NO.
- `in` con needles-grep: locus ya anclado, SIEMPRE un símbolo (`path.cpp:fn`). PROHIBIDO `in` de un .cpp suelto y PROHIBIDO `in":"stem::módulo"` (eso es `campo`). Con cerca, `in` es un array de barrios (`[]` = inmediaciones).
- cerrar: tú decides cuándo termina. Si entendiste el objeto (o qué falta), cierra. El runtime no te retiene porque Circuito esté incompleto.
- No repitas needles que ya están en el cuaderno. `foo` y `stem::foo` son la misma aguja. Si hits=0 en el grafo, cambia de SÍMBOLO, no de cualificación. Un grep `in` distinto del mismo keyword sí vale.
- No repitas un peek o follow que ya está en ya leídos / ya seguidos. Llamadores = hops del follow, no otro follow del mismo símbolo.
- juicio: keep/drop solo ids que aparecen en Candidatas (vale M1 del atlas).
- peek: leer 1–3 loci. Función = cuerpo (si es largo: firma + cola). Header = API del .hpp. Bajo el cuerpo: rayos rankeados (ficha de impacto × consulta): callers y calls, 1–2 hops. Se marcan no leído y otro stem. No es el follow (stacks).
- follow: callers (quién llama) Y callees (qué llama, con cond). Stacks, ramas ON/CXL/OFF, mermaid. Hops peekables. No es el grep ni la firma del peek.
- entre: camino dirigido en el registry entre DOS loci ya anclados (`from` → `to`). No es el mermaid de follow. `sin camino` también es evidencia. Hops intermedios peekables.
- tanda: en UNA ola, needles y/o peeks y/o follows y/o entre. Un `in` malo no cancela los peeks.
- independiente: un papel del Guion sigue [sin evidencia] y peek/follow/cerca en lo anclado no te dan ese locus. Entonces: me queda esto; sácalo a un ciclo aparte. Índice 1-based. El runtime usa ESA frase como consulta; no redactes un prompt. Vuelve el pack (visto/huecos/cuerpos), no un recap. No anida. El runtime rechaza si el índice no vale o si ese papel ya tiene evidencia. [sin evidencia] no es "no busco".
- cerrar: síntesis de lo entendido / lo que falta. Primera frase: encontraste el objeto de TU consulta, o no. Si lo leído es otra cosa, dilo; no asignes la siguiente caza. Termina. Legal en cualquier ola de piloto. Un papel [sin evidencia] no se cierra como "no aplica": si no sale de lo anclado y independiente aún cabe, sácalo. `huecos` opcional: nombres que afirmas y no leíste (no es una lista de deberes). No recetes un parche en un símbolo no leído. El why no cubre una pregunta del guion que no hayas leído.

El bosquejo del Circuito une lo leído y emite rayos a hops no leídos (callers/calls). Rayo = no leído; al peekearlo pasa a sólido. Un rayo que cambia de stem puede mostrar quién más llama a ese hop. PROHIBIDO inventar el camino.

Campo opcional recorta el grep (stem o prefijo de path).

JSON:
{"action":"ola_v1","do":"needles","needles":["start_job"],"why":"cazar el arranque del objeto"}
{"action":"ola_v1","do":"needles","in":"src/pkg/mod.cpp:run_job","needles":["stop_job","start_job"],"why":"¿todos los returns paran el trabajo?"}
{"action":"ola_v1","do":"cerca","needles":["idle timeout"],"in":[],"why":"rank de conceptos en las inmediaciones"}
{"action":"ola_v1","do":"cerca","needles":["stale handle"],"in":["src/pkg"],"why":"rank en ese barrio"}
{"action":"ola_v1","do":"juicio","keep":["M1"],"drop":["M2"],"why":"esta zona cubre el objeto de la consulta"}
{"action":"ola_v1","do":"peek","peeks":["M1","src/pkg/mod.hpp"],"why":"cuerpo del ancla y API del header"}
{"action":"ola_v1","do":"follow","follows":["M1"],"why":"flujo: quién llama y a quién llama"}
{"action":"ola_v1","do":"entre","from":"start_job","to":"stop_job","why":"hay camino del arranque a la parada"}
{"action":"ola_v1","do":"tanda","peeks":["M1","M7"],"follows":["M7"],"why":"cuerpos y flujo en una ola"}
{"action":"ola_v1","do":"independiente","papel":1,"why":"me queda esto; no sale de lo anclado"}
{"action":"ola_v1","do":"cerrar","why":"el control vive en pkg::run_job","huecos":["run_job_async"]}
)";
}

std::string wave_pilot_user_prompt(const WaveState& st) {
  std::ostringstream out;
  if (wave_is_last_propose(st)) {
    out << "ÚLTIMA OLA. El runtime cierra después si hace falta.\n\n";
  }
  out << wave_strategy_markdown() << "\n";
  if (!st.control_worker) {
    out << wave_guion_markdown(st) << "\n";
  }
  out << "Elige UNA ola.\n";
  if (!st.control_worker) {
    const std::string cue = wave_independiente_cue_markdown(st);
    if (!cue.empty()) {
      out << cue;
    }
  }
  out << "\n";
  out << "## Consulta\n" << st.prompt << "\n\n";
  out << wave_pin_cue_markdown(st);
  out << wave_work_markdown(st);
  return out.str();
}

std::string wave_explorer_system_prompt() {
  return R"(Eres el EXPLORADOR. NO editas código. NO lanzas otros ciclos. NO independiente. NO orquestas.
Cada respuesta es UNA ola. Tras ver el cuaderno eliges el siguiente gesto.
PROHIBIDO un plan congelado de varias olas. PROHIBIDO inventar ids.

El cuaderno de trabajo es la evidencia. Peeks y Follows se ACUMULAN (cuerpos, stacks y recortes). Atlas es hipótesis de retrieval. Mermaid no está aquí.
Si un peek/follow ya está en ya leídos / ya seguidos, léelo en Peeks/Follows; NO lo pidas otra vez.
Peek vale sobre ids, símbolos, menciones, hops, o un archivo listado en files (el header suele bastar para ver la API).
La Estrategia es el orden de comprensión: primero el sistema, luego el verbo, luego disparo y efecto por separado. No mezcles ramas.
Cover keep localiza el sistema de ESTA consulta, no un plan de varios objetos.
Afirmar un símbolo sin haberlo leído es el mismo delito que citar un id inventado.

do:
- needles: agujas de MECANISMO (símbolos, APIs), no sinónimos del prompt. El runtime busca substring en id/path/symbol/stem. `stem::simbolo` busca el símbolo recortado a ese stem.
- cerca: `needles` son conceptos de 1–3 palabras, NO identificadores y NO una frase. El runtime embebe solo esos términos (el why no entra) y rankea fichas en el barrio: peeks + keep + rayos, o `in` (M*, path:fn, stem, prefijo). Un `in` de directorio o .cpp despierta el grafo ahí. hops=1 (tope 2). hits=0 SÍ es ausencia en ese barrio.
- `in` con needles-grep: locus ya anclado, SIEMPRE un símbolo (`path.cpp:fn`). PROHIBIDO `in` de un .cpp suelto.
- No repitas needles que ya están en el cuaderno. Si hits=0 en el grafo, cambia de SÍMBOLO.
- No repitas un peek o follow que ya está en ya leídos / ya seguidos.
- peek: leer 1–3 loci. Función = cuerpo. Header = API del .hpp. Rayos callers/calls. No es el follow.
- follow: callers Y callees (cond). Stacks, ramas ON/CXL/OFF. Hops peekables.
- entre: camino dirigido entre DOS loci ya anclados. `sin camino` también es evidencia.
- tanda: needles y/o peeks y/o follows y/o entre en UNA ola.
- cerrar: primera frase: encontraste el objeto de TU consulta, o no. Si lo leído es otra cosa: no encontré lo que preguntaba; lo leído es X. PROHIBIDO asignar la siguiente caza. `huecos` opcional: nombres afirmados y no leídos. No recetes un parche en un símbolo no leído.

JSON:
{"action":"ola_v1","do":"needles","needles":["start_job"],"why":"cazar el arranque del objeto"}
{"action":"ola_v1","do":"cerca","needles":["idle timeout"],"in":[],"why":"rank de conceptos en las inmediaciones"}
{"action":"ola_v1","do":"peek","peeks":["M1","src/pkg/mod.hpp"],"why":"cuerpo del ancla y API del header"}
{"action":"ola_v1","do":"follow","follows":["M1"],"why":"flujo: quién llama y a quién llama"}
{"action":"ola_v1","do":"entre","from":"start_job","to":"stop_job","why":"hay camino del arranque a la parada"}
{"action":"ola_v1","do":"tanda","peeks":["M1"],"follows":["M1"],"why":"cuerpo y flujo en una ola"}
{"action":"ola_v1","do":"cerrar","why":"no encontré el arranque del trabajo; lo leído formatea un string","huecos":["run_job_async"]}
)";
}

bool wave_control_bosquejar_ok(const std::vector<std::string>& conceptos, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (static_cast<int>(conceptos.size()) < kWaveControlBosquejarMin ||
      static_cast<int>(conceptos.size()) > kWaveControlBosquejarMax) {
    return set("control: bosquejar 2–8 conceptos");
  }
  std::unordered_set<std::string> seen;
  for (const auto& n : conceptos) {
    if (!wave_cerca_concept_ok(n)) {
      return set("control: cada concepto es 1–3 palabras, no frase ni id");
    }
    const std::string key = ascii_lower(trim_ws_copy(n));
    if (!seen.insert(key).second) {
      return set("control: conceptos repetidos");
    }
  }
  return true;
}

bool wave_control_aguja_ok(const std::string& tok) {
  const std::string t = trim_ws_copy(tok);
  if (!wave_cerca_concept_ok(t)) {
    return false;
  }
  if (papel_token_is_zone_id(t)) {
    return false;
  }
  if (t.find('_') != std::string::npos) {
    return false;
  }
  const bool one_word = t.find(' ') == std::string::npos;
  if (one_word && std::isupper(static_cast<unsigned char>(t[0])) != 0) {
    return false;
  }
  return true;
}

bool wave_control_agujas_ok(const std::vector<std::string>& agujas, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (static_cast<int>(agujas.size()) < kWaveControlAgujasMin ||
      static_cast<int>(agujas.size()) > kWaveControlAgujasMax) {
    return set("control: agujas 2–8 zonas");
  }
  std::unordered_set<std::string> seen;
  for (const auto& n : agujas) {
    if (!wave_control_aguja_ok(n)) {
      return set("control: cada aguja es una clase de sitio (event, mouse, ai), no ident");
    }
    const std::string key = ascii_lower(trim_ws_copy(n));
    if (!seen.insert(key).second) {
      return set("control: agujas repetidas");
    }
  }
  return true;
}

int wave_control_agujas_merge(std::vector<std::string>* acc, const std::vector<std::string>& add) {
  if (acc == nullptr) {
    return 0;
  }
  std::unordered_set<std::string> seen;
  for (const auto& a : *acc) {
    seen.insert(ascii_lower(trim_ws_copy(a)));
  }
  int n = 0;
  for (const auto& raw : add) {
    const std::string t = trim_ws_copy(raw);
    const std::string key = ascii_lower(t);
    if (key.empty() || !seen.insert(key).second) {
      continue;
    }
    acc->push_back(t);
    ++n;
  }
  return n;
}

bool wave_control_zoom_id_ok(const std::string& id) {
  const std::string t = trim_ws_copy(id);
  if (t.size() < 2 || t.size() > 48) {
    return false;
  }
  if (papel_token_is_zone_id(t)) {
    return false;
  }
  for (std::size_t i = 0; i < t.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c == '/' || c == '\\' || c == ':' || c == '.' || c == ' ') {
      return false;
    }
    if (std::isalnum(c) == 0 && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

bool wave_control_consulta_ok(const std::string& consulta, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  const std::string t = trim_ws_copy(consulta);
  if (t.size() < 8) {
    return set("control: consulta demasiado corta");
  }
  if (t.size() > static_cast<std::size_t>(kWaveControlConsultaChars)) {
    return set("control: consulta demasiado larga");
  }
  for (std::size_t i = 0; i < t.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c == '/' || c == ':' || c == '_') {
      return set("control: consulta sin path, id ni stem");
    }
    if (c == '.' && i > 0 && i + 1 < t.size() &&
        std::isalnum(static_cast<unsigned char>(t[i - 1])) != 0 &&
        std::isalnum(static_cast<unsigned char>(t[i + 1])) != 0) {
      return set("control: consulta sin path, id ni stem");
    }
  }
  const auto words = papel_words(t);
  if (static_cast<int>(words.size()) < kWaveControlConsultaWordsMin ||
      static_cast<int>(words.size()) > kWaveControlConsultaWordsMax) {
    return set("control: consulta 4–120 palabras");
  }
  for (const auto& w : words) {
    if (papel_token_is_zone_id(w)) {
      return set("control: consulta sin M*");
    }
  }
  const std::string low = ascii_lower(t);
  auto has = [&](const char* k) { return low.find(k) != std::string::npos; };
  const bool cat_file = has("archivo") || has("archivos") || has("fichero") || has("ficheros");
  const bool file_obj = has("escritura") || has("escrito") || has("a medias") || has("buffer") ||
                        has("revert") || has("reviert") || has("revers") || has("deshac") ||
                        has("rollback") || has("restaur") || has("insert") || has("pendiente") ||
                        has("guardad");
  if (cat_file && !file_obj) {
    return set("control: consulta sin objeto (no una categoría)");
  }
  if (has("función que") || has("funcion que")) {
    return set("control: consulta plantilla, no un prompt de investigación");
  }
  return true;
}

bool wave_control_evita_ok(const std::vector<std::string>& evita, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (evita.empty()) {
    return true;
  }
  if (static_cast<int>(evita.size()) > kWaveControlEvitaMax) {
    return set("control: evita máx 3 conceptos");
  }
  for (const auto& e : evita) {
    if (!wave_cerca_concept_ok(e)) {
      return set("control: evita es un concepto de 1–3 palabras, no id ni path");
    }
  }
  return true;
}

bool wave_control_cerrar_why_ok(const std::string& why, std::string* err) {
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  const std::string low = ascii_lower(trim_ws_copy(why));
  if (low.find("queda contestada") != std::string::npos ||
      low.find("preguntamos y leímos") != std::string::npos ||
      low.find("lo que preguntamos") != std::string::npos ||
      low.find("con lo que leímos") != std::string::npos) {
    return set("control: cierre vacío, cita el mecanismo leído");
  }
  return true;
}

namespace {

std::vector<std::string> control_content_words(const std::string& s) {
  std::vector<std::string> out;
  for (const auto& w : papel_words(s)) {
    if (w.size() < 4 || guion_stopword(w)) {
      continue;
    }
    out.push_back(ascii_lower(w));
  }
  return out;
}

bool control_consulta_shell_word(const std::string& w) {
  static const char* kShell[] = {"define",  "definir", "función", "funcion", "handler",
                                 "invoca",  "invocar", "llama",   "llamar"};
  for (const char* s : kShell) {
    if (w == s) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> control_consulta_caza_words(const std::string& s) {
  std::vector<std::string> out;
  for (const auto& w : control_content_words(s)) {
    if (control_consulta_shell_word(w)) {
      continue;
    }
    bool dup = false;
    for (const auto& e : out) {
      if (e == w) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out.push_back(w);
    }
  }
  return out;
}

int control_word_hits(const std::vector<std::string>& needle,
                      const std::vector<std::string>& hay) {
  int n = 0;
  for (const auto& w : needle) {
    for (const auto& h : hay) {
      if (w == h) {
        ++n;
        break;
      }
    }
  }
  return n;
}

bool control_consulta_verb(const std::string& w) {
  static const char* kVerb[] = {
      "aborta",     "abortar",     "busca",      "buscar",      "cancela",    "cancelar",
      "captura",    "capturar",    "cubre",      "cubrir",      "detiene",    "detener",
      "entra",      "entrar",      "encuentra",  "encontrar",   "evalúa",     "evalua",
      "evaluar",    "intercepta",  "interceptar","invoca",      "invocar",    "lee",
      "leen",       "limpia",      "limpiar",    "maneja",      "manejar",    "muestra",
      "mostrar",    "nombra",      "nombrar",    "pregunta",    "preguntar",  "pulsa",
      "pulsar",     "registra",    "registrar",  "sigue",       "seguir",     "traduce",
      "traducir",   "vincula",     "vincular",   "vive",        "vivir"};
  for (const char* v : kVerb) {
    if (w == v) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> control_consulta_objetos(const std::string& s) {
  std::vector<std::string> out;
  for (const auto& w : control_content_words(s)) {
    if (control_consulta_verb(w)) {
      continue;
    }
    bool dup = false;
    for (const auto& e : out) {
      if (e == w) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out.push_back(w);
    }
  }
  return out;
}

bool control_caza_no_encontro(const std::string& thesis) {
  const std::string low = ascii_lower(trim_ws_copy(thesis));
  if (low.empty() || low == "(vacío)") {
    return false;
  }
  if (low.rfind("leído:", 0) == 0) {
    return true;
  }
  return low.find("engañoso") != std::string::npos || low.find("no es") != std::string::npos ||
         low.find("no un") != std::string::npos || low.find("no la captura") != std::string::npos ||
         low.find("no el disparo") != std::string::npos ||
         low.find("no el objeto") != std::string::npos || low.find("no era") != std::string::npos ||
         low.find("no encontr") != std::string::npos ||
         low.find("no se encontr") != std::string::npos ||
         low.find("sanitiz") != std::string::npos || low.find("formate") != std::string::npos ||
         low.find("utilidad") != std::string::npos;
}

std::string control_jobs_last_cerrado(const std::string& jobs_md) {
  const auto pos = jobs_md.rfind("Cerrado:");
  if (pos == std::string::npos) {
    return {};
  }
  std::string rest = jobs_md.substr(pos + 8);
  const char* stops[] = {"\nAbierto:", "\nHuecos de esta caza", "\n### ", "\n# "};
  std::size_t cut = rest.size();
  for (const char* s : stops) {
    const auto p = rest.find(s);
    if (p != std::string::npos && p < cut) {
      cut = p;
    }
  }
  return trim_ws_copy(rest.substr(0, cut));
}

bool control_jobs_last_failed(const std::string& jobs_md) {
  return control_caza_no_encontro(control_jobs_last_cerrado(jobs_md));
}

bool control_token_is_id(const std::string& t) {
  if (t.empty()) {
    return true;
  }
  if (t.find('_') != std::string::npos || t.find('/') != std::string::npos ||
      t.find(':') != std::string::npos || t.find('`') != std::string::npos) {
    return true;
  }
  if (papel_token_is_zone_id(t)) {
    return true;
  }
  bool seen_lower = false;
  for (unsigned char c : t) {
    if (c >= 0x80) {
      return false;
    }
    if (std::islower(c) != 0) {
      seen_lower = true;
    } else if (std::isupper(c) != 0 && seen_lower) {
      return true;
    }
  }
  return false;
}

bool control_hueco_is_id(const std::string& h) {
  const std::string t = trim_ws_copy(h);
  if (t.size() < 4) {
    return true;
  }
  if (t.find(' ') == std::string::npos && t.find('\t') == std::string::npos) {
    return true;
  }
  if (control_token_is_id(t)) {
    return true;
  }
  for (const auto& w : papel_words(t)) {
    if (papel_token_is_zone_id(w) || control_token_is_id(w)) {
      return true;
    }
  }
  return false;
}

bool control_token_is_path_crumb(const std::string& t) {
  const std::string low = ascii_lower(t);
  return low == "src" || low == "cpp" || low == "hpp" || low == "hxx" || low == "cc";
}

bool control_token_is_circuit_noise(const std::string& t) {
  const std::string low = ascii_lower(t);
  return low == "on" || low == "off" || low == "via" || low == "entre";
}

void control_append_kept(std::string* out, const std::string& tok) {
  if (out->empty()) {
    *out += tok;
    return;
  }
  const unsigned char last = static_cast<unsigned char>(out->back());
  const unsigned char first = static_cast<unsigned char>(tok.front());
  if ((std::isalnum(last) != 0 || last >= 0x80) &&
      (std::isalnum(first) != 0 || first >= 0x80)) {
    out->push_back(' ');
  }
  *out += tok;
}

std::string control_strip_id_tokens(std::string s) {
  for (;;) {
    const auto a = s.find('`');
    if (a == std::string::npos) {
      break;
    }
    auto b = s.find('`', a + 1);
    if (b == std::string::npos) {
      s.erase(a);
      break;
    }
    s.erase(a, b + 1 - a);
  }
  std::string out;
  std::string cur;
  auto keep_one = [&](const std::string& tok) {
    if (tok.empty() || control_token_is_id(tok) || control_token_is_path_crumb(tok) ||
        control_token_is_circuit_noise(tok)) {
      return;
    }
    control_append_kept(&out, tok);
  };
  auto flush = [&]() {
    if (cur.empty()) {
      return;
    }
    int trailing_dots = 0;
    while (!cur.empty() && cur.back() == '.') {
      cur.pop_back();
      ++trailing_dots;
    }
    const bool slash_pair = cur.find('/') != std::string::npos &&
                            cur.find('.') == std::string::npos &&
                            cur.find('_') == std::string::npos &&
                            cur.find(':') == std::string::npos;
    const std::size_t before = out.size();
    if (slash_pair) {
      std::string part;
      for (char c : cur) {
        if (c == '/') {
          keep_one(part);
          part.clear();
        } else {
          part.push_back(c);
        }
      }
      keep_one(part);
    } else if (cur.find('/') != std::string::npos || cur.find(':') != std::string::npos ||
               cur.find('.') != std::string::npos) {
      // path entero: no dejar src/cpp
    } else {
      keep_one(cur);
    }
    if (trailing_dots > 0 && out.size() > before) {
      out.push_back('.');
    }
    cur.clear();
  };
  for (unsigned char c : s) {
    if (std::isalnum(c) != 0 || c >= 0x80 || c == '_' || c == '/' || c == ':' || c == '.') {
      cur.push_back(static_cast<char>(c));
    } else {
      flush();
      if (c == '\n') {
        if (!out.empty() && out.back() != '\n') {
          out.push_back('\n');
        }
      } else if (std::isspace(c) != 0) {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
          out.push_back(' ');
        }
      } else if (c == '(' || c == ')' || c == '[' || c == ']') {
        continue;
      } else if (c == ',' || c == ';' || c == '!' || c == '?' || c == '\'' ||
                 c == '"') {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
          out.push_back(static_cast<char>(c));
        }
      }
    }
  }
  flush();
  return trim_ws_copy(out);
}

std::string control_strip_fences(std::string why) {
  for (;;) {
    const auto a = why.find("```");
    if (a == std::string::npos) {
      break;
    }
    auto b = why.find("```", a + 3);
    if (b == std::string::npos) {
      why.erase(a);
      break;
    }
    why.erase(a, b + 3 - a);
  }
  return why;
}

std::string control_split_camel(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (i > 0 && std::isupper(c) != 0 &&
        std::islower(static_cast<unsigned char>(s[i - 1])) != 0) {
      out.push_back(' ');
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string control_locus_role(std::string loc) {
  loc = trim_ws_copy(loc);
  if (loc.size() >= 2 && loc.front() == '`' && loc.back() == '`') {
    loc = loc.substr(1, loc.size() - 2);
  }
  const auto paren = loc.find('(');
  if (paren != std::string::npos) {
    loc.resize(paren);
  }
  const auto slash = loc.find_last_of("/\\");
  if (slash != std::string::npos && slash + 1 < loc.size()) {
    loc = loc.substr(slash + 1);
  } else if (slash != std::string::npos) {
    loc.clear();
  }
  const auto colon = loc.rfind(':');
  if (colon != std::string::npos && colon + 1 < loc.size()) {
    loc = loc.substr(colon + 1);
  }
  if (!loc.empty() && loc[0] == '/') {
    loc.erase(0, 1);
  }
  std::string spaced;
  for (char c : loc) {
    if (c == '_' || c == '-' || c == '>' || c == '.') {
      if (!spaced.empty() && spaced.back() != ' ') {
        spaced.push_back(' ');
      }
    } else if (std::isalnum(static_cast<unsigned char>(c)) != 0 ||
               static_cast<unsigned char>(c) >= 0x80) {
      spaced.push_back(c);
    } else if (!spaced.empty() && spaced.back() != ' ') {
      spaced.push_back(' ');
    }
  }
  spaced = trim_ws_copy(control_split_camel(spaced));
  const std::string low = ascii_lower(spaced);
  if (spaced.empty() || control_token_is_path_crumb(spaced) || control_token_is_circuit_noise(low) ||
      papel_token_is_zone_id(spaced)) {
    return {};
  }
  return spaced;
}

bool control_hop_is_noise(const std::string& loc, const std::string& kind) {
  if (ascii_lower(kind) == "ctrl") {
    return true;
  }
  const std::string low_loc = ascii_lower(loc);
  if (low_loc.find(":if") != std::string::npos || low_loc.find(":else") != std::string::npos ||
      low_loc.find(":then") != std::string::npos || low_loc.find(":case") != std::string::npos ||
      low_loc.find("guard") != std::string::npos) {
    return true;
  }
  const std::string role = control_locus_role(loc);
  if (role.empty()) {
    return true;
  }
  const std::string low = ascii_lower(role);
  if (low == "if" || low == "else" || low == "then" || low == "case" || low == "min" ||
      low == "max" || low == "event" || low == "busy" || low == "append" || low == "load" ||
      low == "lock" || low == "empty" || low == "string" || low == "cancel" || low == "guard") {
    return true;
  }
  if (role.find(' ') == std::string::npos && role.size() < 8) {
    return true;
  }
  return false;
}

std::vector<std::string> control_extract_visto_roles(const std::string& raw) {
  std::vector<std::string> out;
  std::istringstream in(raw);
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim_ws_copy(line);
    if (t.rfind("Visto:", 0) != 0) {
      continue;
    }
    std::string rest = trim_ws_copy(t.substr(6));
    if (rest == "(nada)") {
      break;
    }
    for (;;) {
      const auto a = rest.find('`');
      if (a == std::string::npos) {
        break;
      }
      const auto b = rest.find('`', a + 1);
      if (b == std::string::npos) {
        break;
      }
      const std::string role = control_locus_role(rest.substr(a + 1, b - a - 1));
      if (!role.empty()) {
        bool dup = false;
        for (const auto& e : out) {
          if (ascii_lower(e) == ascii_lower(role)) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          out.push_back(role);
        }
      }
      rest.erase(0, b + 1);
    }
    break;
  }
  return out;
}

std::string control_thesis_keep_roles(std::string s) {
  for (;;) {
    const auto a = s.find('`');
    if (a == std::string::npos) {
      break;
    }
    const auto b = s.find('`', a + 1);
    if (b == std::string::npos) {
      s.erase(a, 1);
      break;
    }
    const std::string role = control_locus_role(s.substr(a + 1, b - a - 1));
    s.replace(a, b + 1 - a, role.empty() ? std::string(" ") : (" " + role + " "));
  }
  std::string out;
  std::string cur;
  auto flush = [&]() {
    if (cur.empty()) {
      return;
    }
    std::string role = cur;
    if (cur.find("src/") != std::string::npos || cur.find("src\\") != std::string::npos ||
        cur.find(".cpp") != std::string::npos || cur.find(".hpp") != std::string::npos ||
        cur.find("::") != std::string::npos ||
        (cur.find('/') != std::string::npos && cur.find(':') != std::string::npos)) {
      role = control_locus_role(cur);
    } else if (cur.find('_') != std::string::npos) {
      role = control_locus_role(cur);
    } else if (cur.find('/') != std::string::npos) {
      for (char& c : role) {
        if (c == '/') {
          c = ' ';
        }
      }
      role = trim_ws_copy(role);
    } else {
      role = trim_ws_copy(control_split_camel(cur));
      if (control_token_is_path_crumb(role) || papel_token_is_zone_id(role) ||
          control_token_is_circuit_noise(ascii_lower(role))) {
        role.clear();
      }
    }
    if (!role.empty()) {
      if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
        out.push_back(' ');
      }
      out += role;
    }
    cur.clear();
  };
  for (unsigned char c : s) {
    if (std::isalnum(c) != 0 || c >= 0x80 || c == '_' || c == '/' || c == ':' || c == '.' ||
        c == '-') {
      cur.push_back(static_cast<char>(c));
    } else {
      flush();
      if (c == '\n') {
        if (!out.empty() && out.back() != '\n') {
          out.push_back('\n');
        }
      } else if (c == ' ' || c == '\t') {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
          out.push_back(' ');
        }
      } else if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?') {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
          out.push_back(static_cast<char>(c));
        }
      }
    }
  }
  flush();
  return trim_ws_copy(out);
}

bool control_thesis_is_circuit_only(const std::string& s) {
  if (s.empty()) {
    return true;
  }
  const std::string low = ascii_lower(s);
  if (low.rfind("on ", 0) == 0 || low.rfind("off ", 0) == 0 || low == "on." || low == "off.") {
    return true;
  }
  bool any = false;
  for (unsigned char c : s) {
    if (std::isalnum(c) != 0 || c >= 0x80) {
      any = true;
      break;
    }
  }
  return !any;
}

std::string control_cierre_thesis(const std::string& cierre) {
  const std::string raw = trim_ws_copy(control_strip_fences(cierre));
  const auto visto = control_extract_visto_roles(raw);
  std::string why = raw;
  const auto mark = why.find("(El why no es evidencia");
  if (mark != std::string::npos) {
    auto nl = why.find('\n', mark);
    why = (nl == std::string::npos) ? std::string() : why.substr(nl + 1);
  }
  std::istringstream in(why);
  std::string line;
  std::ostringstream body;
  while (std::getline(in, line)) {
    const std::string t = trim_ws_copy(line);
    if (t.empty() || t.rfind("Guion:", 0) == 0 || t.rfind("Preguntas", 0) == 0 ||
        t.rfind("(Afirmar", 0) == 0 || t.rfind("Visto:", 0) == 0 || t.rfind("Huecos:", 0) == 0) {
      continue;
    }
    const std::string low = ascii_lower(t);
    if (low.rfind("on ", 0) == 0 || low.rfind("off ", 0) == 0) {
      continue;
    }
    if (!body.str().empty()) {
      body << '\n';
    }
    body << t;
  }
  why = control_thesis_keep_roles(trim_ws_copy(body.str()));
  if (control_thesis_is_circuit_only(why)) {
    if (visto.empty()) {
      return {};
    }
    std::ostringstream leido;
    leido << "leído:";
    for (std::size_t i = 0; i < visto.size() && i < 8; ++i) {
      if (i) {
        leido << ",";
      }
      leido << " " << visto[i];
    }
    why = leido.str();
  }
  if (why.size() > 1200) {
    utf8_resize(&why, 1200);
  }
  return why;
}

}  // namespace

bool wave_control_consulta_es_ancla(const std::string& consulta, const std::string& ancla) {
  const std::string a = ascii_lower(trim_ws_copy(consulta));
  const std::string b = ascii_lower(trim_ws_copy(ancla));
  return !a.empty() && a == b;
}

bool wave_control_consulta_delta_ok(const std::string& consulta, const std::string& ancla,
                                   const std::vector<std::string>& prev, std::string* err) {
  (void)prev;
  auto set = [&](const char* m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  if (wave_control_consulta_es_ancla(consulta, ancla)) {
    return set("control: consulta recap del ancla, no un prompt de investigación");
  }
  return true;
}

std::string wave_control_consulta_misma_caza(const std::string& consulta,
                                            const std::vector<std::string>& prev) {
  const std::string q = trim_ws_copy(consulta);
  if (q.empty()) {
    return {};
  }
  const auto objs = control_consulta_objetos(q);
  for (const auto& p : prev) {
    const std::string prev_q = trim_ws_copy(p);
    if (prev_q.empty()) {
      continue;
    }
    if (prev_q == q) {
      return prev_q;
    }
    const auto prev_objs = control_consulta_objetos(prev_q);
    const int hit = control_word_hits(objs, prev_objs);
    if (hit >= 2) {
      return prev_q;
    }
  }
  return {};
}

WaveControlOla wave_parse_control(const std::string& raw) {
  WaveControlOla out;
  const std::string blob = extract_action_json(raw);
  if (blob.empty()) {
    out.error = "control sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(blob);
  } catch (const std::exception& e) {
    out.error = std::string("JSON control inválido: ") + e.what();
    return out;
  }
  const std::string action = j.value("action", "");
  if (action != "control_v1") {
    out.error = "contrato control_v1 inválido";
    return out;
  }
  const std::string d = json_str(j, "do");
  if (d == "explorar") {
    out.do_kind = WaveControlDo::Explorar;
  } else if (d == "ampliar") {
    out.do_kind = WaveControlDo::Ampliar;
  } else if (d == "zoom") {
    out.do_kind = WaveControlDo::Zoom;
  } else if (d == "agujas") {
    out.do_kind = WaveControlDo::Agujas;
  } else if (d == "bosquejar") {
    out.do_kind = WaveControlDo::Bosquejar;
  } else if (d == "cerrar") {
    out.do_kind = WaveControlDo::Cerrar;
  } else if (d == "plan") {
    out.do_kind = WaveControlDo::Plan;
  } else if (d == "pasar") {
    out.do_kind = WaveControlDo::Pasar;
  } else if (d == "no_pasar") {
    out.do_kind = WaveControlDo::NoPasar;
  } else if (d == "revisar") {
    out.do_kind = WaveControlDo::Revisar;
  } else {
    out.error = "control do inválido";
    return out;
  }
  out.why = json_str(j, "why");
  if (out.why.size() < 8) {
    out.error = "why demasiado corto";
    return out;
  }
  if (out.why.size() > 800) {
    utf8_resize(&out.why, 800);
  }
  if (out.do_kind == WaveControlDo::Cerrar) {
    std::string cerr;
    if (!wave_control_cerrar_why_ok(out.why, &cerr)) {
      out.error = cerr.empty() ? "control: cierre vacío, cita el mecanismo leído" : cerr;
      return out;
    }
  }
  if (out.do_kind == WaveControlDo::Explorar) {
    std::vector<std::string> raw = json_str_array(j, "consultas");
    if (raw.empty()) {
      const std::string one = json_str(j, "consulta");
      if (!one.empty()) {
        raw.push_back(one);
      }
    }
    if (raw.empty()) {
      out.error = "control: consulta o consultas";
      return out;
    }
    if (static_cast<int>(raw.size()) > kWaveControlMaxConsultas) {
      out.error = "control: máx 1 explorador por turno";
      return out;
    }
    for (auto& c : raw) {
      c = control_strip_id_tokens(c);
    }
    out.consultas = raw;
    out.consulta = raw.front();
    for (const auto& c : raw) {
      std::string cerr;
      if (!wave_control_consulta_ok(c, &cerr)) {
        out.error = cerr.empty() ? "consulta inválida" : cerr;
        return out;
      }
    }
    for (std::size_t i = 0; i < raw.size(); ++i) {
      for (std::size_t k = i + 1; k < raw.size(); ++k) {
        if (ascii_lower(raw[i]) == ascii_lower(raw[k])) {
          out.error = "control: consultas repetidas";
          return out;
        }
      }
    }
    out.hacia = json_str_array(j, "hacia");
    wave_control_sanitize_hacia(&out.hacia);
    out.evita = json_str_array(j, "evita");
    {
      std::string eerr;
      if (!wave_control_evita_ok(out.evita, &eerr)) {
        out.error = eerr.empty() ? "evita inválido" : eerr;
        return out;
      }
    }
  }
  if (out.do_kind == WaveControlDo::Revisar) {
    out.consulta = control_strip_id_tokens(json_str(j, "consulta"));
    out.hacia = json_str_array(j, "hacia");
    wave_control_sanitize_hacia(&out.hacia);
    if (out.consulta.empty() && out.hacia.empty()) {
      out.error = "control: revisar consulta o hacia";
      return out;
    }
    if (!out.consulta.empty()) {
      std::string cerr;
      if (!wave_control_consulta_ok(out.consulta, &cerr)) {
        out.error = cerr.empty() ? "consulta inválida" : cerr;
        return out;
      }
    }
    wave_control_sanitize_hacia(&out.hacia);
    out.evita = json_str_array(j, "evita");
    {
      std::string eerr;
      if (!wave_control_evita_ok(out.evita, &eerr)) {
        out.error = eerr.empty() ? "evita inválido" : eerr;
        return out;
      }
    }
  }
  if (out.do_kind == WaveControlDo::Bosquejar) {
    out.hacia = json_str_array(j, "hacia");
    if (out.hacia.empty()) {
      out.hacia = json_str_array(j, "conceptos");
    }
    std::string herr;
    if (!wave_control_bosquejar_ok(out.hacia, &herr)) {
      out.error = herr.empty() ? "control: bosquejar 2–8 conceptos" : herr;
      return out;
    }
  }
  if (out.do_kind == WaveControlDo::Agujas) {
    out.hacia = json_str_array(j, "agujas");
    if (out.hacia.empty()) {
      out.hacia = json_str_array(j, "needles");
    }
    if (out.hacia.empty()) {
      out.hacia = json_str_array(j, "hacia");
    }
    std::string herr;
    if (!wave_control_agujas_ok(out.hacia, &herr)) {
      out.error = herr.empty() ? "control: agujas 2–8 idents" : herr;
      return out;
    }
  }
  if (out.do_kind == WaveControlDo::Plan) {
    if (j.contains("pasos") || (j.contains("plan") && j["plan"].is_array() &&
                                !j["plan"].empty() && j["plan"].front().is_string())) {
      out.error = "control: plan con fases, no pasos";
      return out;
    }
    const std::string modo = ascii_lower(json_str(j, "modo"));
    if (modo == "romper") {
      out.plan.modo = WaveControlPlanModo::Romper;
    } else if (modo == "seguir") {
      out.plan.modo = WaveControlPlanModo::Seguir;
    } else {
      out.error = "control: plan modo romper o seguir";
      return out;
    }
    if (!j.contains("fases") || !j["fases"].is_array()) {
      out.error = "control: plan 2–4 fases";
      return out;
    }
    const auto& arr = j["fases"];
    if (static_cast<int>(arr.size()) < kWaveControlPlanFasesMin ||
        static_cast<int>(arr.size()) > kWaveControlPlanFasesMax) {
      out.error = "control: plan 2–4 fases";
      return out;
    }
    std::unordered_set<std::string> ids;
    int n_locator = 0;
    int n_seguir = 0;
    std::vector<std::string> locator_ids;
    for (const auto& item : arr) {
      if (!item.is_object()) {
        out.error = "control: fase no es objeto";
        return out;
      }
      WaveControlPhase ph;
      ph.id = json_str(item, "id");
      if (ph.id.empty() || ph.id.size() > 8) {
        out.error = "control: id de fase 1–8";
        return out;
      }
      for (char c : ph.id) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
          out.error = "control: id de fase alfanumérico";
          return out;
        }
      }
      if (ids.count(ph.id) != 0) {
        out.error = "control: id de fase repetido";
        return out;
      }
      ids.insert(ph.id);
      const std::string kind = ascii_lower(json_str(item, "kind"));
      if (kind == "locator") {
        ph.kind = WaveControlPhaseKind::Locator;
        ++n_locator;
      } else if (kind == "puente") {
        ph.kind = WaveControlPhaseKind::Puente;
      } else if (kind == "seguir") {
        ph.kind = WaveControlPhaseKind::Seguir;
        ++n_seguir;
      } else {
        out.error = "control: kind locator, puente o seguir";
        return out;
      }
      ph.consulta = control_strip_id_tokens(json_str(item, "consulta"));
      ph.need = json_str_array(item, "need");
      ph.hacia = json_str_array(item, "hacia");
      ph.evita = json_str_array(item, "evita");
      {
        std::string eerr;
        if (!wave_control_evita_ok(ph.evita, &eerr)) {
          out.error = eerr.empty() ? "evita inválido" : eerr;
          return out;
        }
      }
      if (ph.kind == WaveControlPhaseKind::Locator) {
        const bool first = n_locator == 1;
        if (first) {
          if (ph.consulta.empty()) {
            out.error = "control: locator sin consulta";
            return out;
          }
          std::string cerr;
          if (!wave_control_consulta_ok(ph.consulta, &cerr)) {
            out.consulta = ph.consulta;
            out.error = cerr.empty() ? "locator inválido" : cerr;
            return out;
          }
        } else {
          if (!ph.consulta.empty()) {
            out.error = "control: locator posterior: hacia, no consulta (se escribe al lanzar)";
            return out;
          }
          wave_control_sanitize_hacia(&ph.hacia);
          if (ph.hacia.empty()) {
            out.error = "control: locator posterior: hacia";
            return out;
          }
        }
        locator_ids.push_back(ph.id);
      } else {
        if (ph.need.empty()) {
          ph.need = locator_ids;
        }
        if (ph.need.empty()) {
          out.error = "control: puente/seguir sin locator previo";
          return out;
        }
        for (const auto& n : ph.need) {
          if (ids.count(n) == 0 || n == ph.id) {
            out.error = "control: need debe ser una fase previa";
            return out;
          }
        }
        if (ph.kind == WaveControlPhaseKind::Seguir) {
          wave_control_sanitize_hacia(&ph.hacia);
          if (ph.hacia.empty()) {
            out.error = "control: seguir sin hacia";
            return out;
          }
        }
      }
      out.plan.fases.push_back(std::move(ph));
    }
    if (n_locator < 1) {
      out.error = "control: plan sin locator";
      return out;
    }
    if (out.plan.modo == WaveControlPlanModo::Seguir && n_seguir < 1) {
      out.error = "control: modo seguir sin fase seguir";
      return out;
    }
  }
  if (out.do_kind == WaveControlDo::Ampliar) {
    auto ids = json_str_array(j, "ids");
    if (ids.empty()) {
      ids = json_str_array(j, "inspect");
    }
    if (ids.empty()) {
      out.error = "control: ampliar 1–3 ids M*";
      return out;
    }
    if (static_cast<int>(ids.size()) > kWaveControlMaxAmpliar) {
      out.error = "control: ampliar máx 3 fichas";
      return out;
    }
    for (const auto& id : ids) {
      if (!papel_token_is_zone_id(id)) {
        out.error = "control: ampliar solo ids M*";
        return out;
      }
    }
    out.ids = std::move(ids);
  }
  if (out.do_kind == WaveControlDo::Zoom) {
    auto ids = json_str_array(j, "ids");
    if (ids.empty()) {
      out.error = "control: zoom 1–3 ids de barrio o stem";
      return out;
    }
    if (static_cast<int>(ids.size()) > kWaveControlMaxZoom) {
      out.error = "control: zoom máx 3 ids";
      return out;
    }
    std::unordered_set<std::string> seen;
    for (const auto& id : ids) {
      if (!wave_control_zoom_id_ok(id)) {
        out.error = "control: zoom id de barrio o stem, no M* ni path";
        return out;
      }
      if (!seen.insert(ascii_lower(id)).second) {
        out.error = "control: zoom ids repetidos";
        return out;
      }
    }
    out.ids = std::move(ids);
  }
  out.ok = true;
  return out;
}

void control_set_err(std::string* err, const char* m) {
  if (err != nullptr) {
    *err = m;
  }
}

int wave_control_plan_current(const WaveControlPlan& plan) {
  for (int i = 0; i < static_cast<int>(plan.fases.size()); ++i) {
    if (plan.fases[static_cast<std::size_t>(i)].status == WaveControlPhaseStatus::EnCurso) {
      return i;
    }
  }
  return -1;
}

const WaveControlPhase* control_find_phase(const WaveControlPlan& plan, const std::string& id) {
  for (const auto& ph : plan.fases) {
    if (ph.id == id) {
      return &ph;
    }
  }
  return nullptr;
}

bool control_need_met(const WaveControlPlan& plan, const WaveControlPhase& ph) {
  if (ph.need.empty()) {
    return true;
  }
  for (const auto& n : ph.need) {
    const auto* dep = control_find_phase(plan, n);
    if (dep == nullptr || dep->status != WaveControlPhaseStatus::Paso) {
      return false;
    }
  }
  return true;
}

int control_next_launch_index(const WaveControlPlan& plan) {
  for (int i = 0; i < static_cast<int>(plan.fases.size()); ++i) {
    const auto& ph = plan.fases[static_cast<std::size_t>(i)];
    if (ph.status != WaveControlPhaseStatus::Pendiente) {
      continue;
    }
    if (ph.kind == WaveControlPhaseKind::Locator) {
      return i;
    }
    if ((ph.kind == WaveControlPhaseKind::Puente || ph.kind == WaveControlPhaseKind::Seguir) &&
        control_need_met(plan, ph)) {
      return i;
    }
  }
  return -1;
}

bool control_begin_first_locator(WaveControlPlan* plan, std::string* err) {
  if (plan == nullptr) {
    control_set_err(err, "control: plan nulo");
    return false;
  }
  const int idx = control_next_launch_index(*plan);
  if (idx < 0) {
    control_set_err(err, "control: plan sin locator lanzable");
    return false;
  }
  plan->fases[static_cast<std::size_t>(idx)].status = WaveControlPhaseStatus::EnCurso;
  return true;
}

std::vector<WaveControlDo> wave_control_legal(const WaveControlPlan& plan, int jobs_run,
                                             bool last_visto, int agujas_n, int zoom_n,
                                             int ampliar_n) {
  std::vector<WaveControlDo> out;
  if (!plan.committed) {
    if (jobs_run <= 0) {
      if (kWaveControlCatalogLive && agujas_n > 0 && zoom_n <= 0) {
        out.push_back(WaveControlDo::Zoom);
        return out;
      }
      if (kWaveControlCatalogLive && agujas_n < kWaveControlMaxAgujas) {
        out.push_back(WaveControlDo::Agujas);
      }
      if (kWaveControlBosquejoLive) {
        out.push_back(WaveControlDo::Bosquejar);
      }
      if (kWaveControlCatalogLive) {
        out.push_back(WaveControlDo::Zoom);
      }
      out.push_back(WaveControlDo::Explorar);
      if (ampliar_n < kWaveControlMaxAmpliarTurns) {
        out.push_back(WaveControlDo::Ampliar);
      }
      out.push_back(WaveControlDo::Plan);
    } else {
      if (jobs_run < kWaveControlMaxJobs) {
        if (kWaveControlCatalogLive && agujas_n < kWaveControlMaxAgujas) {
          out.push_back(WaveControlDo::Agujas);
        }
        if (kWaveControlBosquejoLive) {
          out.push_back(WaveControlDo::Bosquejar);
        }
        if (kWaveControlCatalogLive) {
          out.push_back(WaveControlDo::Zoom);
        }
        out.push_back(WaveControlDo::Explorar);
        out.push_back(WaveControlDo::Plan);
      } else {
        out.push_back(WaveControlDo::Plan);
      }
      out.push_back(WaveControlDo::Cerrar);
    }
    return out;
  }
  const auto add_soltar = [&]() {
    if (jobs_run > 0 && jobs_run < kWaveControlMaxJobs) {
      out.push_back(WaveControlDo::Explorar);
    }
    if (jobs_run > 0) {
      out.push_back(WaveControlDo::Plan);
      out.push_back(WaveControlDo::Cerrar);
    }
  };
  const int cur = wave_control_plan_current(plan);
  if (cur < 0) {
    add_soltar();
    return out;
  }
  const auto status = plan.fases[static_cast<std::size_t>(cur)].status;
  if (status == WaveControlPhaseStatus::EnCurso) {
    if (last_visto) {
      out.push_back(WaveControlDo::Pasar);
    }
    out.push_back(WaveControlDo::NoPasar);
    out.push_back(WaveControlDo::Revisar);
    add_soltar();
  } else if (status == WaveControlPhaseStatus::Fallo) {
    out.push_back(WaveControlDo::Revisar);
    add_soltar();
  } else if (jobs_run > 0) {
    add_soltar();
  }
  return out;
}

bool wave_control_do_allowed(const std::vector<WaveControlDo>& legal, WaveControlDo d) {
  return std::find(legal.begin(), legal.end(), d) != legal.end();
}

std::string wave_control_legal_markdown(const std::vector<WaveControlDo>& legal) {
  std::ostringstream out;
  out << "Legal ahora:";
  if (legal.empty()) {
    out << " (ninguno)\n";
    return out.str();
  }
  for (std::size_t i = 0; i < legal.size(); ++i) {
    out << (i == 0 ? " " : ", ") << wave_control_do_name(legal[i]);
  }
  out << "\n";
  return out.str();
}

std::string wave_control_plan_markdown(const WaveControlPlan& plan) {
  if (!plan.committed && plan.fases.empty()) {
    return {};
  }
  std::ostringstream out;
  const char* modo = wave_control_plan_modo_name(plan.modo);
  out << "Plan de búsqueda";
  if (modo[0] != '\0') {
    out << " (" << modo << ")";
  }
  out << ":\n";
  for (std::size_t i = 0; i < plan.fases.size(); ++i) {
    const auto& ph = plan.fases[i];
    out << (i + 1) << ". [" << wave_control_phase_status_name(ph.status) << "] " << ph.id << " "
        << wave_control_phase_kind_name(ph.kind);
    if (!ph.consulta.empty()) {
      out << ": " << ph.consulta;
    }
    if (!ph.need.empty()) {
      out << " (need";
      for (const auto& n : ph.need) {
        out << " " << n;
      }
      out << ")";
    }
    if (!ph.hacia.empty()) {
      out << " (hacia";
      for (const auto& h : ph.hacia) {
        out << " " << h;
      }
      out << ")";
    }
    out << "\n";
  }
  return out.str();
}

nlohmann::json wave_control_plan_to_json(const WaveControlPlan& plan) {
  nlohmann::json fases = nlohmann::json::array();
  for (const auto& ph : plan.fases) {
    fases.push_back({{"id", ph.id},
                     {"kind", wave_control_phase_kind_name(ph.kind)},
                     {"consulta", ph.consulta},
                     {"need", ph.need},
                     {"hacia", ph.hacia},
                     {"evita", ph.evita},
                     {"status", wave_control_phase_status_name(ph.status)},
                     {"ancla_visto", ph.ancla_visto}});
  }
  return {{"committed", plan.committed},
          {"modo", wave_control_plan_modo_name(plan.modo)},
          {"fases", fases}};
}

bool wave_control_plan_commit(WaveControlPlan* plan, const WaveControlOla& ola, std::string* err) {
  if (plan == nullptr) {
    control_set_err(err, "control: plan nulo");
    return false;
  }
  if (ola.do_kind != WaveControlDo::Plan || ola.plan.fases.empty()) {
    control_set_err(err, "control: plan 2–4 fases");
    return false;
  }
  *plan = ola.plan;
  plan->committed = true;
  for (auto& ph : plan->fases) {
    ph.status = WaveControlPhaseStatus::Pendiente;
    ph.ancla_visto.clear();
  }
  return control_begin_first_locator(plan, err);
}

bool wave_control_plan_pasar(WaveControlPlan* plan, const std::vector<std::string>& visto,
                            std::string* err) {
  if (plan == nullptr || !plan->committed) {
    control_set_err(err, "control: no hay plan");
    return false;
  }
  const int cur = wave_control_plan_current(*plan);
  if (cur < 0 || plan->fases[static_cast<std::size_t>(cur)].status !=
                     WaveControlPhaseStatus::EnCurso) {
    control_set_err(err, "control: no hay fase en curso");
    return false;
  }
  if (visto.empty()) {
    control_set_err(err, "control: pasar exige locus leído (visto)");
    return false;
  }
  auto& ph = plan->fases[static_cast<std::size_t>(cur)];
  ph.status = WaveControlPhaseStatus::Paso;
  ph.ancla_visto = visto;
  const int next = control_next_launch_index(*plan);
  if (next >= 0) {
    plan->fases[static_cast<std::size_t>(next)].status = WaveControlPhaseStatus::EnCurso;
  }
  return true;
}

bool wave_control_plan_no_pasar(WaveControlPlan* plan, std::string* err) {
  if (plan == nullptr || !plan->committed) {
    control_set_err(err, "control: no hay plan");
    return false;
  }
  const int cur = wave_control_plan_current(*plan);
  if (cur < 0 || plan->fases[static_cast<std::size_t>(cur)].status !=
                     WaveControlPhaseStatus::EnCurso) {
    control_set_err(err, "control: no hay fase en curso");
    return false;
  }
  plan->fases[static_cast<std::size_t>(cur)].status = WaveControlPhaseStatus::Fallo;
  plan->fases[static_cast<std::size_t>(cur)].ancla_visto.clear();
  const int next = control_next_launch_index(*plan);
  if (next >= 0) {
    plan->fases[static_cast<std::size_t>(next)].status = WaveControlPhaseStatus::EnCurso;
  }
  return true;
}

bool wave_control_plan_revisar(WaveControlPlan* plan, const std::string& consulta,
                              const std::vector<std::string>& hacia, std::string* err,
                              const std::vector<std::string>& evita) {
  if (plan == nullptr || !plan->committed) {
    control_set_err(err, "control: no hay plan");
    return false;
  }
  const int cur = wave_control_plan_current(*plan);
  if (cur < 0) {
    control_set_err(err, "control: no hay fase que revisar");
    return false;
  }
  auto& ph = plan->fases[static_cast<std::size_t>(cur)];
  if (ph.status != WaveControlPhaseStatus::EnCurso) {
    control_set_err(err, "control: revisar solo la fase en curso");
    return false;
  }
  if (ph.kind == WaveControlPhaseKind::Locator) {
    if (consulta.empty()) {
      control_set_err(err, "control: revisar el locator con una consulta");
      return false;
    }
    ph.consulta = consulta;
  } else if (ph.kind == WaveControlPhaseKind::Seguir) {
    if (!hacia.empty()) {
      ph.hacia = hacia;
    }
    if (!consulta.empty()) {
      ph.consulta = consulta;
    }
    if (ph.hacia.empty()) {
      control_set_err(err, "control: seguir sin hacia");
      return false;
    }
  }
  if (!evita.empty()) {
    ph.evita = evita;
  }
  ph.status = WaveControlPhaseStatus::EnCurso;
  ph.ancla_visto.clear();
  return true;
}

std::string control_pin_symbol(const std::string& loc) {
  const std::string tail = symbol_tail(loc);
  return tail.empty() ? loc : tail;
}

WaveControlLaunch wave_control_launch_spec(const WaveControlPlan& plan) {
  WaveControlLaunch spec;
  const int cur = wave_control_plan_current(plan);
  if (cur < 0) {
    return spec;
  }
  const auto& ph = plan.fases[static_cast<std::size_t>(cur)];
  if (ph.status != WaveControlPhaseStatus::EnCurso) {
    return spec;
  }
  spec.kind = ph.kind;
  spec.evita = ph.evita;
  auto collect_need_visto = [&](std::vector<std::string>* all, std::string* first) {
    for (const auto& nid : ph.need) {
      const auto* dep = control_find_phase(plan, nid);
      if (dep == nullptr) {
        continue;
      }
      for (const auto& v : dep->ancla_visto) {
        if (all != nullptr) {
          all->push_back(v);
        }
        if (first != nullptr && first->empty() && !v.empty()) {
          *first = v;
        }
      }
    }
  };
  if (ph.kind == WaveControlPhaseKind::Locator) {
    spec.consulta = ph.consulta;
    spec.ok = !spec.consulta.empty();
    return spec;
  }
  if (ph.kind == WaveControlPhaseKind::Puente) {
    spec.consulta = "si hay camino entre los loci ya anclados";
    std::string from_loc;
    std::string to_loc;
    if (ph.need.size() >= 2) {
      const auto* a = control_find_phase(plan, ph.need[0]);
      const auto* b = control_find_phase(plan, ph.need[1]);
      if (a != nullptr && !a->ancla_visto.empty()) {
        from_loc = a->ancla_visto.front();
      }
      if (b != nullptr && !b->ancla_visto.empty()) {
        to_loc = b->ancla_visto.front();
      }
    } else {
      collect_need_visto(&spec.pin_loci, &from_loc);
      if (spec.pin_loci.size() >= 2) {
        to_loc = spec.pin_loci[1];
      }
    }
    collect_need_visto(&spec.pin_loci, nullptr);
    spec.pin_from = control_pin_symbol(from_loc);
    spec.pin_to = control_pin_symbol(to_loc);
    spec.ok = !spec.pin_from.empty() && !spec.pin_to.empty();
    return spec;
  }
  if (ph.kind == WaveControlPhaseKind::Seguir) {
    spec.consulta = ph.consulta.empty() ? "flujo desde el locus leído hacia los conceptos pedidos"
                                        : ph.consulta;
    std::string from_loc;
    collect_need_visto(&spec.pin_loci, &from_loc);
    spec.pin_from = control_pin_symbol(from_loc);
    spec.hacia = ph.hacia;
    spec.cerca_hops_max = kWaveControlSeguirHops;
    spec.peek_hop_depth = kWaveControlSeguirHops;
    spec.ok = !spec.pin_from.empty();
    return spec;
  }
  return spec;
}

void wave_control_inherit_visto(WaveControlLaunch* spec, const std::vector<std::string>& visto) {
  if (spec == nullptr || visto.empty()) {
    return;
  }
  auto has = [&](const std::string& loc) {
    const std::string low = ascii_lower(loc);
    if (!spec->pin_from.empty() && ascii_lower(spec->pin_from) == low) {
      return true;
    }
    if (!spec->pin_to.empty() && ascii_lower(spec->pin_to) == low) {
      return true;
    }
    for (const auto& e : spec->pin_loci) {
      if (ascii_lower(e) == low) {
        return true;
      }
    }
    return false;
  };
  for (const auto& v : visto) {
    if (v.empty() || has(v)) {
      continue;
    }
    spec->pin_loci.push_back(v);
    if (static_cast<int>(spec->pin_loci.size()) >= kWaveControlInheritVistoMax) {
      break;
    }
  }
}

void wave_control_seed_launch(WaveState* st, const WaveControlLaunch& spec) {
  if (st == nullptr) {
    return;
  }
  st->pin_from = spec.pin_from;
  st->pin_to = spec.pin_to;
  st->pin_loci = spec.pin_loci;
  st->pin_hacia = spec.hacia;
  st->pin_evita = spec.evita;
  st->pin_fallo_md = spec.fallos_md;
  st->cerca_hops_max = spec.cerca_hops_max;
  st->peek_hop_depth = spec.peek_hop_depth;
  auto push_m = [&](const std::string& s) {
    if (s.empty()) {
      return;
    }
    for (const auto& m : st->mencionados) {
      if (ascii_lower(m) == ascii_lower(s)) {
        return;
      }
    }
    st->mencionados.push_back(s);
  };
  push_m(spec.pin_from);
  push_m(spec.pin_to);
  for (const auto& loc : spec.pin_loci) {
    push_m(loc);
    push_m(control_pin_symbol(loc));
  }
}

std::string wave_control_brief(const WaveState& st) {
  const auto h = wave_pack_handoff(st);
  std::ostringstream out;
  out << "consulta: " << st.prompt << "\n";
  out << "keep:";
  if (st.opened_ids.empty()) {
    out << " (ninguno)\n";
  } else {
    for (const auto& id : st.opened_ids) {
      out << " " << id;
    }
    out << "\n";
  }
  const std::string thesis = control_cierre_thesis(st.cierre);
  std::vector<std::string> leido;
  auto push_role = [&](const std::string& r) {
    if (r.empty()) {
      return;
    }
    const std::string low = ascii_lower(r);
    for (const auto& e : leido) {
      if (ascii_lower(e) == low) {
        return;
      }
    }
    leido.push_back(r);
  };
  for (const auto& v : h.visto) {
    push_role(control_locus_role(v));
  }
  for (const auto& v : control_extract_visto_roles(st.cierre)) {
    push_role(v);
  }
  out << "leído:";
  if (leido.empty()) {
    out << " (nada)\n";
  } else {
    for (std::size_t i = 0; i < leido.size() && i < 8; ++i) {
      if (i) {
        out << ",";
      }
      out << " " << leido[i];
    }
    out << "\n";
  }
  const bool caza_fallo = control_caza_no_encontro(thesis) && !st.prompt.empty();
  out << "Cerrado:\n";
  if (caza_fallo) {
    out << "No se encontró el objeto de esta caza.\n";
    out << "Preguntó: " << st.prompt << "\n";
    out << "Lo leído no responde a esa pregunta. Si rehaces, evita lo leído.\n"
           "El siguiente tiro es la clase (el sistema que lo contiene), no el mismo nombre en "
           "otro barrio.\n";
  }
  if (thesis.empty()) {
    out << "(vacío)\n";
  } else {
    out << thesis << "\n";
  }
  std::vector<std::string> roles;
  for (const auto& a : h.huecos) {
    std::string role = control_hueco_is_id(a) ? control_locus_role(a) : a;
    if (role.empty() || control_hueco_is_id(role)) {
      continue;
    }
    const std::string low = ascii_lower(role);
    bool dup = false;
    for (const auto& e : roles) {
      if (ascii_lower(e) == low) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    roles.push_back(role);
    if (static_cast<int>(roles.size()) >= kWaveControlAbiertoCap) {
      break;
    }
  }
  if (caza_fallo) {
    out << "Huecos de esta caza (si el particular falló, la clase puede ser el siguiente "
           "encargo; evita lo leído):\n";
  } else {
    out << "Abierto:\n";
  }
  if (roles.empty()) {
    out << "(nada; no copiar ids del pack)\n";
  } else {
    for (const auto& a : roles) {
      out << "- " << a << "\n";
    }
  }
  return out.str();
}

WaveControlJobSnap wave_control_job_snap(int n, const WaveState& st) {
  WaveControlJobSnap s;
  s.n = n;
  s.consulta = st.prompt;
  s.cerrado = control_cierre_thesis(st.cierre);
  const auto h = wave_pack_handoff(st);
  s.visto = h.visto;
  for (const auto& f : h.seguido) {
    bool have = false;
    for (const auto& v : s.visto) {
      if (locus_keys_match(v, f)) {
        have = true;
        break;
      }
    }
    if (!have && !f.empty()) {
      s.visto.push_back(f);
    }
  }
  s.peek_neighbors = st.peek_neighbors;
  s.circuit_entre = st.circuit_entre;
  return s;
}

namespace {

bool control_job_has_loc(const std::vector<std::string>& visto, const std::string& loc) {
  if (loc.empty()) {
    return false;
  }
  for (const auto& v : visto) {
    if (locus_keys_match(v, loc)) {
      return true;
    }
  }
  return false;
}

void control_push_role_cap(std::vector<std::string>* out, const std::string& loc, int cap) {
  if (out == nullptr) {
    return;
  }
  const std::string role = control_locus_role(loc);
  if (role.empty()) {
    return;
  }
  if (cap > 0 && static_cast<int>(out->size()) >= cap) {
    return;
  }
  const std::string low = ascii_lower(role);
  for (const auto& e : *out) {
    if (ascii_lower(e) == low) {
      return;
    }
  }
  out->push_back(role);
}

std::string control_join_roles(const std::vector<std::string>& roles) {
  std::ostringstream out;
  for (std::size_t i = 0; i < roles.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << roles[i];
  }
  return out.str();
}

std::string control_arrow_roles(const std::string& raw) {
  if (raw.empty()) {
    return {};
  }
  if (ascii_lower(raw).find("sin camino") != std::string::npos) {
    return {};
  }
  std::vector<std::string> parts;
  std::string cur;
  auto flush = [&]() {
    const std::string tok = trim_ws_copy(cur);
    cur.clear();
    if (tok.empty() || control_hop_is_noise(tok, "")) {
      return;
    }
    const std::string role = control_locus_role(tok);
    if (role.empty()) {
      return;
    }
    const std::string low = ascii_lower(role);
    for (const auto& p : parts) {
      if (ascii_lower(p) == low) {
        return;
      }
    }
    parts.push_back(role);
  };
  for (std::size_t i = 0; i < raw.size();) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (i + 2 < raw.size() && c == 0xe2 && static_cast<unsigned char>(raw[i + 1]) == 0x86 &&
        static_cast<unsigned char>(raw[i + 2]) == 0x92) {
      flush();
      i += 3;
      continue;
    }
    if (i + 1 < raw.size() && raw[i] == '-' && raw[i + 1] == '>') {
      flush();
      i += 2;
      continue;
    }
    cur.push_back(raw[i]);
    ++i;
  }
  flush();
  if (parts.size() < 2) {
    return {};
  }
  std::ostringstream out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out << " → ";
    }
    out << parts[i];
  }
  return out.str();
}

std::vector<std::string> control_job_extra_locs(const WaveControlJobSnap& job) {
  std::vector<std::string> extra;
  auto consider = [&](const std::string& loc) {
    if (loc.empty() || control_job_has_loc(job.visto, loc) || control_hop_is_noise(loc, "")) {
      return;
    }
    for (const auto& e : extra) {
      if (locus_keys_match(e, loc)) {
        return;
      }
    }
    extra.push_back(loc);
  };
  for (const auto& n : job.peek_neighbors) {
    for (const auto& c : n.callers) {
      consider(c.loc);
    }
    for (const auto& c : n.callees) {
      consider(c.loc);
    }
    for (const auto& c : n.export_callers) {
      consider(c.loc);
    }
  }
  return extra;
}

bool control_try_registry_entre(const WaveControlPathFn& path_between, const std::string& from,
                                const std::string& to, std::vector<std::string>* hop_roles) {
  if (!path_between || hop_roles == nullptr || from.empty() || to.empty()) {
    return false;
  }
  if (locus_keys_match(from, to)) {
    return false;
  }
  std::string md;
  std::string err;
  std::vector<WaveHit> hops;
  if (!path_between(from, to, &md, &hops, &err)) {
    return false;
  }
  if (hops.size() < 2) {
    return false;
  }
  hop_roles->clear();
  for (const auto& h : hops) {
    std::string loc;
    if (!h.path.empty() && !h.symbol.empty()) {
      loc = h.path + ":" + h.symbol;
    } else if (!h.symbol.empty()) {
      loc = h.symbol;
    } else {
      loc = h.id;
    }
    if (control_hop_is_noise(loc, h.kind)) {
      continue;
    }
    control_push_role_cap(hop_roles, loc, 6);
  }
  return hop_roles->size() >= 2;
}

}  // namespace

std::string wave_control_jobs_circuit_pack(const std::vector<WaveControlJobSnap>& jobs,
                                           const WaveControlPathFn& path_between) {
  if (jobs.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "# control_opened_v1\n";
  out << "n=" << jobs.size() << "  (visto+hops extra+circuito; sin código)\n";
  std::vector<std::vector<std::string>> extras(jobs.size());
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    extras[i] = control_job_extra_locs(jobs[i]);
    const int n = jobs[i].n > 0 ? jobs[i].n : static_cast<int>(i + 1);
    out << "\nT" << n << "\n";
    std::vector<std::string> visto_roles;
    for (const auto& v : jobs[i].visto) {
      control_push_role_cap(&visto_roles, v, kWaveControlCircuitVistoCap);
    }
    out << "    visto: "
        << (visto_roles.empty() ? std::string("(nada)") : control_join_roles(visto_roles)) << "\n";
    std::vector<std::string> extra_roles;
    for (const auto& e : extras[i]) {
      control_push_role_cap(&extra_roles, e, kWaveControlCircuitPeekCap);
    }
    if (!extra_roles.empty()) {
      out << "    extra: " << control_join_roles(extra_roles) << "\n";
    }
    const std::string interno = control_arrow_roles(jobs[i].circuit_entre);
    if (!interno.empty()) {
      out << "    entre interno: " << interno << "\n";
    }
  }
  std::vector<std::string> entre_lines;
  std::vector<std::string> resto_lines;
  auto push_line = [](std::vector<std::string>* dst, const std::string& line, int cap) {
    if (dst == nullptr || line.empty()) {
      return;
    }
    for (const auto& e : *dst) {
      if (e == line) {
        return;
      }
    }
    if (static_cast<int>(dst->size()) >= cap) {
      return;
    }
    dst->push_back(line);
  };
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    const int ni = jobs[i].n > 0 ? jobs[i].n : static_cast<int>(i + 1);
    for (std::size_t j = i + 1; j < jobs.size(); ++j) {
      const int nj = jobs[j].n > 0 ? jobs[j].n : static_cast<int>(j + 1);
      std::string shared;
      for (const auto& v : jobs[i].visto) {
        if (control_job_has_loc(jobs[j].visto, v)) {
          const std::string role = control_locus_role(v);
          if (!role.empty()) {
            shared = role;
            break;
          }
        }
      }
      std::string sketch;
      auto touch = [&](const std::vector<std::string>& extra,
                       const std::vector<std::string>& visto) {
        for (const auto& e : extra) {
          if (control_job_has_loc(visto, e)) {
            const std::string role = control_locus_role(e);
            if (!role.empty()) {
              return role;
            }
          }
        }
        return std::string{};
      };
      sketch = touch(extras[i], jobs[j].visto);
      if (sketch.empty()) {
        sketch = touch(extras[j], jobs[i].visto);
      }
      std::vector<std::string> a_only;
      for (const auto& v : jobs[i].visto) {
        if (!control_job_has_loc(jobs[j].visto, v)) {
          a_only.push_back(v);
        }
      }
      std::vector<std::string> b_only;
      for (const auto& v : jobs[j].visto) {
        if (!control_job_has_loc(jobs[i].visto, v)) {
          b_only.push_back(v);
        }
      }
      std::vector<std::string> hop_roles;
      bool registry = false;
      if (path_between) {
        const std::size_t a_n = std::min(a_only.size(), static_cast<std::size_t>(3));
        const std::size_t b_n = std::min(b_only.size(), static_cast<std::size_t>(3));
        int tries = 0;
        for (std::size_t ai = 0; ai < a_n && !registry; ++ai) {
          for (std::size_t bi = 0; bi < b_n && !registry; ++bi) {
            if (tries++ >= kWaveControlCircuitPairTries) {
              break;
            }
            if (control_try_registry_entre(path_between, a_only[ai], b_only[bi], &hop_roles) ||
                control_try_registry_entre(path_between, b_only[bi], a_only[ai], &hop_roles)) {
              registry = true;
            }
          }
        }
      }
      auto prefix = [&]() {
        std::ostringstream line;
        line << "  T" << ni << "=>T" << nj << "  ";
        return line.str();
      };
      if (!shared.empty()) {
        push_line(&entre_lines, prefix() + "mismo objeto: " + shared, kWaveControlCircuitEntreCap);
      }
      if (registry) {
        std::ostringstream chain;
        for (std::size_t h = 0; h < hop_roles.size(); ++h) {
          if (h) {
            chain << " → ";
          }
          chain << hop_roles[h];
        }
        push_line(&entre_lines, prefix() + chain.str(), kWaveControlCircuitEntreCap);
      } else if (shared.empty()) {
        if (!sketch.empty()) {
          push_line(&entre_lines, prefix() + "extra toca visto: " + sketch,
                    kWaveControlCircuitEntreCap);
        } else if (path_between) {
          push_line(&entre_lines, prefix() + "sin camino", kWaveControlCircuitEntreCap);
        } else {
          push_line(&entre_lines, prefix() + "(ningún port)", kWaveControlCircuitEntreCap);
        }
      }
    }
  }
  if (jobs.size() >= 2) {
    out << "\nentre abiertas:\n";
    if (entre_lines.empty()) {
      out << "  (ningún port)\n";
    } else {
      for (const auto& line : entre_lines) {
        out << line << "\n";
      }
    }
  }
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    const int ni = jobs[i].n > 0 ? jobs[i].n : static_cast<int>(i + 1);
    int shown = 0;
    for (const auto& e : extras[i]) {
      bool in_any = false;
      for (const auto& job : jobs) {
        if (control_job_has_loc(job.visto, e)) {
          in_any = true;
          break;
        }
      }
      if (in_any) {
        continue;
      }
      const std::string role = control_locus_role(e);
      if (role.empty()) {
        continue;
      }
      std::ostringstream line;
      line << "  T" << ni << " → " << role;
      push_line(&resto_lines, line.str(), kWaveControlCircuitRestoCap);
      if (++shown >= kWaveControlCircuitPeekCap) {
        break;
      }
    }
  }
  if (!resto_lines.empty()) {
    out << "hacia el resto:\n";
    for (const auto& line : resto_lines) {
      out << line << "\n";
    }
  }
  return out.str();
}

std::string wave_control_fallos_cue(const std::vector<WaveControlJobSnap>& jobs) {
  std::ostringstream out;
  bool any = false;
  for (const auto& job : jobs) {
    if (!control_caza_no_encontro(job.cerrado)) {
      continue;
    }
    if (!any) {
      out << "Caza anterior no encontró su objeto. No reabras ese keep/peek; "
             "no es el objeto de ESTA consulta. El particular falló: no lo parafrasees.\n";
      any = true;
    }
    const int n = job.n > 0 ? job.n : 1;
    out << "T" << n << " preguntó: "
        << (job.consulta.empty() ? "(vacío)" : trim_ws_copy(job.consulta)) << "\n";
    std::vector<std::string> roles;
    for (const auto& v : job.visto) {
      const std::string role = control_locus_role(v);
      if (role.empty()) {
        continue;
      }
      bool dup = false;
      for (const auto& e : roles) {
        if (ascii_lower(e) == ascii_lower(role)) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        roles.push_back(role);
      }
      if (roles.size() >= 4) {
        break;
      }
    }
    out << "Lo leído no era eso:";
    if (roles.empty()) {
      out << " (nada anclado)";
    } else {
      for (std::size_t i = 0; i < roles.size(); ++i) {
        out << (i ? "," : "") << " " << roles[i];
      }
    }
    out << "\n";
    const std::string thesis = trim_ws_copy(job.cerrado);
    if (!thesis.empty()) {
      std::string clip = thesis;
      if (clip.size() > 220) {
        utf8_resize(&clip, 220);
      }
      out << clip << "\n";
    }
  }
  return out.str();
}

std::string wave_control_slice_exam(const std::string& ancla,
                                   const std::vector<WaveControlJobSnap>& jobs) {
  if (jobs.empty() || ancla.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "# examen\n";
  out << "Ancla:\n" << trim_ws_copy(ancla) << "\n";
  for (const auto& job : jobs) {
    const int n = job.n > 0 ? job.n : 1;
    out << "\nT" << n << " preguntó:\n";
    out << (job.consulta.empty() ? "(vacío)" : trim_ws_copy(job.consulta)) << "\n";
    out << "Cerrado de T" << n << ":\n";
    if (control_caza_no_encontro(job.cerrado)) {
      out << "No se encontró el objeto de esta caza. Si rehaces, evita lo leído.\n";
    }
    out << (job.cerrado.empty() ? "(vacío)" : trim_ws_copy(job.cerrado)) << "\n";
  }
  const auto& last = jobs.back();
  if (control_caza_no_encontro(last.cerrado)) {
    out << "\nLa última caza no encontró su objeto. El siguiente tiro es la clase "
           "(el sistema que lo contiene), no el mismo nombre en otro barrio. Un hueco que "
           "nombra esa clase sí vale. evita lo leído. El pack (entre abiertas / extra) dice "
           "el arco, no el ancla.\n";
  } else if (!trim_ws_copy(last.cerrado).empty() && trim_ws_copy(last.cerrado) != "(vacío)") {
    out << "\nLa última caza cerró. El siguiente tiro es un port del pack (entre abiertas / "
           "extra), no el ancla ni el mismo keep.\n";
  }
  return out.str();
}

std::string wave_control_atlas_brief(const std::string& atlas_md) {
  if (atlas_md.empty()) {
    return {};
  }
  std::istringstream in(atlas_md);
  std::ostringstream out;
  out << "Atlas (hipótesis de retrieval, no el código. Apunta en rol; no copies M* ni stems):\n";
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim_ws_copy(line);
    if (t.empty() || t.rfind("#", 0) == 0 || t.rfind("<!--", 0) == 0) {
      continue;
    }
    if (t.rfind("view:", 0) == 0 || t.rfind("consulta:", 0) == 0 || t.rfind("search:", 0) == 0) {
      continue;
    }
    if (t.rfind("peek:", 0) == 0 || t.rfind("port:", 0) == 0 || t.rfind("peek-edge:", 0) == 0 ||
        t.rfind("nucleus:", 0) == 0 || t.rfind("gap:", 0) == 0 || t.rfind("bridges:", 0) == 0 ||
        t.rfind("holes:", 0) == 0) {
      continue;
    }
    std::string kept = t;
    for (char& c : kept) {
      if (c == '_') {
        c = ' ';
      }
    }
    out << kept << "\n";
  }
  std::string s = trim_ws_copy(out.str());
  if (s.size() > 4000) {
    utf8_resize(&s, 4000);
  }
  return s;
}

std::string wave_control_module_map(const std::string& workspace_root) {
  namespace fs = std::filesystem;
  if (workspace_root.empty()) {
    return {};
  }
  const fs::path root(workspace_root);
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    return {};
  }
  auto names_in = [&](const fs::path& dir) {
    std::vector<std::string> names;
    if (!fs::is_directory(dir, ec)) {
      return names;
    }
    for (fs::directory_iterator it(dir, ec); it != fs::directory_iterator() && !ec;
         it.increment(ec)) {
      const auto p = it->path();
      const std::string n = p.filename().string();
      if (n.empty() || n[0] == '.' || n.rfind("._", 0) == 0) {
        continue;
      }
      if (n == "build" || n == "CMakeFiles" || n == "_deps") {
        continue;
      }
      if (it->is_directory(ec)) {
        names.push_back(n);
      }
    }
    std::sort(names.begin(), names.end());
    return names;
  };
  std::ostringstream out;
  out << "Mapa de barrios (nombres, no paths. Tú no compilas ni lees archivos):\n";
  const auto src = names_in(root / "src");
  if (!src.empty()) {
    out << "src:";
    for (const auto& n : src) {
      out << " " << n;
    }
    out << "\n";
  }
  out << "repo:";
  for (const char* extra : {"tests", "tools", "cmake", "docs", "examples"}) {
    if (fs::is_directory(root / extra, ec)) {
      out << " " << extra;
    }
  }
  out << "\nproyecto: CMake\n";
  return trim_ws_copy(out.str());
}

std::string wave_control_barrio_of_path(const std::string& t) {
  auto p = t.find("src/");
  if (p == std::string::npos) {
    p = t.find("src\\");
  }
  if (p == std::string::npos) {
    return {};
  }
  std::string rest = t.substr(p + 4);
  const auto slash = rest.find_first_of("/\\");
  if (slash == std::string::npos || slash == 0) {
    return {};
  }
  return rest.substr(0, slash);
}

std::string wave_control_bosquejo_markdown(const WaveControlBosquejo& foto) {
  std::ostringstream out;
  out << "bosquejo (no es el código; no copies nombres ni ids):\n";
  if (foto.olores.empty()) {
    out << "  olores: (sin hits en el grafo)\n";
  } else {
    out << "  olores:\n";
    for (const auto& o : foto.olores) {
      out << "    " << o.concepto << " ~ ";
      if (o.barrio.empty() || o.hits <= 0) {
        out << "(sin olor)";
      } else {
        out << o.barrio;
        if (o.concentration > 0.f) {
          out << " (c=" << std::fixed << std::setprecision(2) << o.concentration;
          if (o.twin && !o.twin_barrio.empty()) {
            out << ", twin " << o.twin_barrio;
          }
          out << ")";
        } else if (o.twin && !o.twin_barrio.empty()) {
          out << " (twin " << o.twin_barrio << ")";
        }
      }
      out << "\n";
    }
  }
  if (foto.entre.empty()) {
    out << "  entre: (sin arista de barrio)\n";
  } else {
    out << "  entre:";
    std::unordered_set<std::string> seen;
    int n = 0;
    for (const auto& e : foto.entre) {
      if (e.from.empty() || e.to.empty() || e.from == e.to) {
        continue;
      }
      const std::string key = e.from < e.to ? (e.from + "→" + e.to) : (e.to + "→" + e.from);
      if (!seen.insert(key).second) {
        continue;
      }
      out << " " << e.from << "→" << e.to;
      if (++n >= 6) {
        break;
      }
    }
    out << "\n";
  }
  if (!foto.nota.empty()) {
    out << "  nota: " << foto.nota << "\n";
  }
  return trim_ws_copy(out.str());
}

namespace {

std::string control_role_spaces(std::string s) {
  for (char& c : s) {
    if (c == '_') {
      c = ' ';
    }
  }
  return s;
}

std::string control_barrio_from_target(const std::string& t) {
  return wave_control_barrio_of_path(t);
}

void control_foreach_target(const nlohmann::json& zone,
                            const std::function<void(const std::string&)>& fn) {
  auto wrap = [&](const nlohmann::json& o) {
    if (!o.is_object()) {
      return;
    }
    for (const char* k : {"target", "from", "to"}) {
      const std::string t = json_str(o, k);
      if (!t.empty()) {
        fn(t);
      }
    }
  };
  auto walk_arr = [&](const char* key) {
    if (!zone.contains(key) || !zone[key].is_array()) {
      return;
    }
    for (const auto& item : zone[key]) {
      if (item.is_array()) {
        for (const auto& inner : item) {
          wrap(inner);
        }
      } else {
        wrap(item);
      }
    }
  };
  walk_arr("representatives");
  walk_arr("anchors");
  walk_arr("ports");
  walk_arr("edges");
  if (zone.contains("mechanism") && zone["mechanism"].is_object()) {
    for (const auto& kv : zone["mechanism"].items()) {
      wrap(kv.value());
    }
  }
}

std::string control_zone_barrio(const nlohmann::json& zone) {
  std::vector<std::string> hits;
  control_foreach_target(zone, [&](const std::string& t) {
    const std::string b = control_barrio_from_target(t);
    if (!b.empty()) {
      hits.push_back(b);
    }
  });
  if (hits.empty()) {
    return "otros";
  }
  std::map<std::string, int> n;
  for (const auto& b : hits) {
    ++n[b];
  }
  std::string best = hits.front();
  int best_n = 0;
  for (const auto& kv : n) {
    if (kv.second > best_n) {
      best = kv.first;
      best_n = kv.second;
    }
  }
  return best;
}

std::string control_zone_owns(const nlohmann::json& zone) {
  for (const char* key : {"primary_stems", "core_stems"}) {
    const auto stems = json_str_array(zone, key);
    if (!stems.empty()) {
      return control_role_spaces(stems.front());
    }
  }
  return {};
}

std::vector<std::string> control_src_dir_names(const std::string& workspace_root) {
  namespace fs = std::filesystem;
  std::vector<std::string> names;
  std::error_code ec;
  const fs::path dir = fs::path(workspace_root) / "src";
  if (!fs::is_directory(dir, ec)) {
    return names;
  }
  for (fs::directory_iterator it(dir, ec); it != fs::directory_iterator() && !ec; it.increment(ec)) {
    const std::string n = it->path().filename().string();
    if (n.empty() || n[0] == '.' || n.rfind("._", 0) == 0) {
      continue;
    }
    if (n == "build" || n == "CMakeFiles" || n == "_deps") {
      continue;
    }
    if (it->is_directory(ec)) {
      names.push_back(n);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

struct ControlZoneRow {
  std::string id;
  std::string kind;
  std::string owns;
  std::string barrio;
  int ov = 0;
};

int control_zone_ov(const nlohmann::json& zone, const std::string& consulta) {
  if (consulta.empty()) {
    return 0;
  }
  std::vector<std::string> qtoks;
  for (const auto& w : papel_words(consulta)) {
    const std::string low = ascii_lower(w);
    if (low.size() < 4 || guion_stopword(w)) {
      continue;
    }
    qtoks.push_back(low);
  }
  std::vector<std::string> hay;
  auto push_parts = [&](std::string tok) {
    tok = ascii_lower(std::move(tok));
    std::string part;
    auto flush = [&]() {
      if (part.size() >= 4) {
        hay.push_back(part);
      }
      part.clear();
    };
    for (char c : tok) {
      if (c == '_' || c == ':' || c == '/' || c == '.' || c == ' ') {
        flush();
      } else {
        part.push_back(c);
      }
    }
    flush();
  };
  for (const char* key : {"primary_stems", "core_stems"}) {
    for (const auto& s : json_str_array(zone, key)) {
      push_parts(s);
    }
  }
  control_foreach_target(zone, [&](const std::string& t) {
    auto c = t.rfind(':');
    if (c != std::string::npos && c + 1 < t.size()) {
      push_parts(t.substr(c + 1));
    }
  });
  auto hit = [&](const std::string& q, const std::string& h) {
    if (q == h) {
      return true;
    }
    return (q.size() >= 4 && h.size() >= 4 && (q.rfind(h, 0) == 0 || h.rfind(q, 0) == 0));
  };
  int n = 0;
  for (const auto& q : qtoks) {
    for (const auto& h : hay) {
      if (hit(q, h)) {
        ++n;
        break;
      }
    }
  }
  return std::min(n, 9);
}

std::string control_zone_kind(const nlohmann::json& zone) {
  for (const auto& risk : zone.value("risks", nlohmann::json::array())) {
    if (risk.is_string()) {
      const auto r = risk.get<std::string>();
      if (r == "promoted_from_uncovered" || r == "uncovered_candidate") {
        return "hole";
      }
    }
  }
  const std::string declared = json_str(zone, "kind");
  if (declared == "hole") {
    return "hole";
  }
  const std::string owns = ascii_lower(control_zone_owns(zone));
  bool latch_nucleus = false;
  bool has_nucleus = false;
  for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
    has_nucleus = true;
    const std::string st = ascii_lower(nucleus.value("state", ""));
    if (st.find("pending") != std::string::npos || st.find("busy") != std::string::npos ||
        st.find("spinner") != std::string::npos || st.find("latch") != std::string::npos ||
        st.find("flag") != std::string::npos) {
      latch_nucleus = true;
    }
  }
  const bool has_ports = !zone.value("ports", nlohmann::json::array()).empty();
  if (latch_nucleus) {
    return "latch";
  }
  if (owns.find("panel") != std::string::npos || owns.find("gutter") != std::string::npos ||
      owns.find("hover") != std::string::npos) {
    return "chrome";
  }
  if (owns.find("modal") != std::string::npos || owns.find("overlay") != std::string::npos ||
      owns.find("strip") != std::string::npos || owns.find("picker") != std::string::npos) {
    return "object";
  }
  if (declared == "latch" || declared == "chrome" || declared == "caller" ||
      declared == "object" || declared == "other") {
    return declared;
  }
  if (has_ports || has_nucleus) {
    return "caller";
  }
  return "other";
}

ControlZoneRow control_zone_row(const nlohmann::json& zone, const std::string& consulta) {
  ControlZoneRow r;
  r.id = zone.value("id", "");
  r.kind = control_zone_kind(zone);
  r.owns = control_zone_owns(zone);
  r.barrio = control_zone_barrio(zone);
  r.ov = control_zone_ov(zone, consulta);
  return r;
}

std::string control_format_zone_line(const ControlZoneRow& r, bool with_barrio) {
  std::ostringstream line;
  line << r.id << "  kind=" << r.kind << "  ov=" << r.ov;
  if (with_barrio && !r.barrio.empty()) {
    line << "  barrio=" << r.barrio;
  }
  if (!r.owns.empty()) {
    line << "  owns: " << r.owns;
  }
  return line.str();
}

std::string control_trim_us(std::string s) {
  while (!s.empty() && s.back() == '_') {
    s.pop_back();
  }
  return s;
}

std::string control_zone_nucleus(const nlohmann::json& zone) {
  for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
    if (!nucleus.is_object()) {
      continue;
    }
    const std::string st = control_trim_us(json_str(nucleus, "state"));
    if (st.size() >= 4) {
      return control_role_spaces(st);
    }
  }
  return {};
}

std::string control_short_tiene(std::string role) {
  role = trim_ws_copy(std::move(role));
  std::vector<std::string> words;
  std::string cur;
  auto flush = [&]() {
    if (!cur.empty()) {
      words.push_back(cur);
      cur.clear();
    }
  };
  for (char c : role) {
    if (c == ' ' || c == '\t') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  if (words.size() >= 3) {
    const auto first = ascii_lower(words.front());
    if (first == "handle" || first == "make" || first == "track" || first == "clear" ||
        first == "try") {
      words.erase(words.begin());
    }
  }
  if (words.size() > 3) {
    words.erase(words.begin(), words.end() - 3);
  }
  std::string out;
  for (const auto& w : words) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += w;
  }
  return out;
}

std::string control_zone_tiene(const nlohmann::json& zone, const std::string& owns) {
  const std::string owns_l = ascii_lower(owns);
  std::vector<std::string> got;
  control_foreach_target(zone, [&](const std::string& t) {
    if (static_cast<int>(got.size()) >= 2) {
      return;
    }
    std::string role = control_short_tiene(control_role_spaces(control_trim_us(symbol_tail(t))));
    const std::string low = ascii_lower(role);
    if (low.size() < 4 || low == owns_l) {
      return;
    }
    if (!owns_l.empty() && (low.find(owns_l) != std::string::npos ||
                            owns_l.find(low) != std::string::npos)) {
      return;
    }
    for (const auto& g : got) {
      if (ascii_lower(g) == low) {
        return;
      }
    }
    got.push_back(std::move(role));
  });
  std::string out;
  for (const auto& g : got) {
    if (!out.empty()) {
      out += ", ";
    }
    out += g;
  }
  return out;
}

std::string control_kind_not(const std::string& kind) {
  if (kind == "latch") {
    return "gesto de tecla o clic";
  }
  if (kind == "chrome") {
    return "latch y disparo (layout)";
  }
  if (kind == "object") {
    return "latch de generación";
  }
  if (kind == "caller") {
    return "objeto del efecto";
  }
  return {};
}

std::string control_format_inspect_card(const nlohmann::json& zone, const std::string& consulta) {
  const auto r = control_zone_row(zone, consulta);
  std::ostringstream line;
  line << r.id << "  kind=" << r.kind << "  ov=" << r.ov;
  if (!r.barrio.empty()) {
    line << "  barrio=" << r.barrio;
  }
  line << "\n";
  const auto nuc = control_zone_nucleus(zone);
  if (!nuc.empty()) {
    line << "    núcleo: " << nuc << "\n";
  }
  const auto tiene = control_zone_tiene(zone, r.owns);
  if (!tiene.empty()) {
    line << "    tiene: " << tiene << "\n";
  }
  const auto noto = control_kind_not(r.kind);
  if (!noto.empty()) {
    line << "    not: " << noto << "\n";
  }
  return line.str();
}

}  // namespace

std::string wave_control_barrio_brief(const nlohmann::json& cards, const std::string& consulta,
                                      const std::string& workspace_root,
                                      const std::vector<std::string>& opened_ids) {
  if (!cards.contains("zones") || !cards["zones"].is_array()) {
    return wave_control_module_map(workspace_root);
  }
  std::unordered_set<std::string> opened;
  for (const auto& id : opened_ids) {
    if (!id.empty()) {
      opened.insert(id);
    }
  }
  const bool showing_rest = !opened.empty();
  std::map<std::string, std::vector<ControlZoneRow>> by;
  std::map<std::string, int> max_ov;
  std::unordered_set<std::string> used;
  std::vector<ControlZoneRow> holes;
  for (const auto& zone : cards["zones"]) {
    if (!zone.is_object()) {
      continue;
    }
    auto r = control_zone_row(zone, consulta);
    if (r.id.empty()) {
      continue;
    }
    if (opened.count(r.id)) {
      if (r.kind != "hole") {
        used.insert(r.barrio);
      }
      continue;
    }
    if (r.kind == "hole") {
      holes.push_back(r);
      continue;
    }
    by[r.barrio].push_back(r);
    max_ov[r.barrio] = std::max(max_ov[r.barrio], r.ov);
    used.insert(r.barrio);
  }
  std::vector<std::string> order;
  for (const auto& kv : by) {
    order.push_back(kv.first);
  }
  std::sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
    if (max_ov[a] != max_ov[b]) {
      return max_ov[a] > max_ov[b];
    }
    return a < b;
  });
  std::ostringstream out;
  if (showing_rest) {
    out << "Resto compacto (aún no abiertas; puedes ampliar 1–3 más o explorar. No copies ids):\n";
  } else {
    out << "Atlas por barrios (hipótesis de retrieval, no el código. "
           "Si ov empatado o duda de objeto, amplia 1–3 M* antes de explorar. "
           "kind=hole que solo rima con el ancla no es el objeto. No copies ids):\n";
  }
  for (const auto& b : order) {
    auto rows = by[b];
    std::sort(rows.begin(), rows.end(), [](const ControlZoneRow& x, const ControlZoneRow& y) {
      if (x.ov != y.ov) {
        return x.ov > y.ov;
      }
      auto kw = [](const std::string& k) {
        if (k == "latch") {
          return 4;
        }
        if (k == "object") {
          return 3;
        }
        if (k == "chrome") {
          return 2;
        }
        if (k == "caller") {
          return 1;
        }
        return 0;
      };
      if (kw(x.kind) != kw(y.kind)) {
        return kw(x.kind) > kw(y.kind);
      }
      return x.id < y.id;
    });
    if (!showing_rest && static_cast<int>(rows.size()) > kWaveControlBarrioKeep) {
      rows.resize(static_cast<std::size_t>(kWaveControlBarrioKeep));
    }
    out << b << "  ov=" << max_ov[b] << "\n";
    for (const auto& r : rows) {
      out << "  " << control_format_zone_line(r, false) << "\n";
    }
  }
  if (showing_rest && by.empty()) {
    out << "(ninguna compacta)\n";
  }
  if (!holes.empty()) {
    std::sort(holes.begin(), holes.end(), [](const ControlZoneRow& x, const ControlZoneRow& y) {
      return x.id < y.id;
    });
    out << "Huecos de retrieval (probable ruido de la consulta; no mandes explorador aquí):";
    for (const auto& r : holes) {
      out << " " << r.id;
      if (!r.owns.empty()) {
        out << "(" << r.owns << ")";
      }
    }
    out << "\n";
  }
  std::vector<std::string> rest;
  for (const auto& n : control_src_dir_names(workspace_root)) {
    if (used.count(n) == 0) {
      rest.push_back(n);
    }
  }
  if (!rest.empty()) {
    out << "También en el repo, sin ficha para esta consulta:";
    for (const auto& n : rest) {
      out << " " << n;
    }
    out << "\n";
  }
  out << "proyecto: CMake\n";
  std::string s = trim_ws_copy(out.str());
  if (s.size() > 4500) {
    utf8_resize(&s, 4500);
  }
  return s;
}

std::string wave_control_inspect_brief(const nlohmann::json& cards,
                                       const std::vector<std::string>& ids,
                                       const std::string& consulta) {
  if (ids.empty()) {
    return {};
  }
  std::unordered_map<std::string, nlohmann::json> by_id;
  for (const auto& zone : cards.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (!id.empty()) {
      by_id[id] = zone;
    }
  }
  std::ostringstream out;
  out << "Fichas ampliadas (kind/núcleo/not. Owns no va en la consulta):\n";
  for (const auto& want : ids) {
    auto it = by_id.find(want);
    if (it == by_id.end()) {
      out << want << "  (no está en el atlas)\n";
      continue;
    }
    out << control_format_inspect_card(it->second, consulta);
  }
  std::string s = trim_ws_copy(out.str());
  if (s.size() > 2500) {
    utf8_resize(&s, 2500);
  }
  return s;
}

std::string control_sanitize_role_md(const std::string& md, std::size_t cap) {
  if (md.empty()) {
    return {};
  }
  std::ostringstream out;
  std::istringstream in(md);
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim_ws_copy(line);
    if (t.empty()) {
      continue;
    }
    if (t.rfind("stems:", 0) == 0 || t.rfind("core stems:", 0) == 0 ||
        t.rfind("context stems:", 0) == 0) {
      continue;
    }
    if (t.rfind("<!--", 0) == 0) {
      continue;
    }
    if (t.rfind("#", 0) == 0 && t.rfind("##", 0) != 0) {
      continue;
    }
    if (t.rfind("query:", 0) == 0 || t.rfind("gate:", 0) == 0 || t.rfind("view:", 0) == 0 ||
        t.rfind("consulta:", 0) == 0 || t.rfind("search:", 0) == 0) {
      continue;
    }
    if (t.find("src/") != std::string::npos || t.find("src\\") != std::string::npos) {
      continue;
    }
    std::string kept = t;
    for (std::size_t i = 0; i + 1 < kept.size();) {
      if (kept[i] == ':' && kept[i + 1] == ':') {
        kept[i] = ' ';
        kept.erase(i + 1, 1);
        continue;
      }
      ++i;
    }
    for (char& c : kept) {
      if (c == '_') {
        c = ' ';
      }
    }
    out << kept << "\n";
  }
  std::string s = trim_ws_copy(out.str());
  if (s.size() > cap) {
    utf8_resize(&s, cap);
  }
  return s;
}

std::string control_fit_opened_cards(std::string md, std::size_t cap) {
  if (md.size() <= cap) {
    return md;
  }
  const std::string mark = "\n## M";
  const std::size_t first = md.find(mark);
  if (first == std::string::npos) {
    utf8_resize(&md, cap);
    return md;
  }
  std::vector<std::string> parts;
  parts.push_back(md.substr(0, first));
  std::size_t pos = first + 1;
  while (true) {
    const std::size_t next = md.find(mark, pos);
    if (next == std::string::npos) {
      parts.push_back(md.substr(pos));
      break;
    }
    parts.push_back(md.substr(pos, next - pos));
    pos = next + 1;
  }
  const std::size_t ncard = parts.size() - 1;
  if (parts[0].size() + 80 >= cap) {
    utf8_resize(&parts[0], cap / 4);
  }
  const std::size_t rest = cap > parts[0].size() ? cap - parts[0].size() : cap / 2;
  const std::size_t each = ncard > 0 ? std::max<std::size_t>(160, rest / ncard) : rest;
  std::string out = parts[0];
  for (std::size_t i = 1; i < parts.size(); ++i) {
    std::string sec = parts[i];
    if (sec.size() > each) {
      utf8_resize(&sec, each);
      if (sec.empty() || sec.back() != '\n') {
        sec += "\n";
      }
      sec += "…\n";
    }
    if (!out.empty() && out.back() != '\n') {
      out += "\n";
    }
    out += sec;
  }
  // No recortar la cola: M10 era la última ficha y el cap lineal la comía.
  return out;
}

std::string wave_control_opened_brief(const std::string& opened_md) {
  if (opened_md.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "Fichas ampliadas (pack+inspect del explorador. Owns/peek no van en la consulta):\n";
  out << control_sanitize_role_md(opened_md, 64 * 1024);
  return control_fit_opened_cards(trim_ws_copy(out.str()),
                                  static_cast<std::size_t>(kWaveControlOpenedChars));
}

std::string wave_control_atlas_pack_brief(const std::string& atlas_md, std::size_t cap) {
  if (atlas_md.empty()) {
    return {};
  }
  if (cap == 0) {
    cap = static_cast<std::size_t>(kWaveControlOpenedChars);
  }
  std::ostringstream out;
  out << "Atlas del explorador (peek/nucleus/port de todo el mazo. Owns no va en la consulta):\n";
  out << control_sanitize_role_md(atlas_md, cap);
  return trim_ws_copy(out.str());
}

std::vector<std::string> wave_control_owns_phrases(const nlohmann::json& cards) {
  std::vector<std::string> out;
  if (!cards.is_object()) {
    return out;
  }
  for (const auto& zone : cards.value("zones", nlohmann::json::array())) {
    if (!zone.is_object()) {
      continue;
    }
    const std::string owns = control_zone_owns(zone);
    if (owns.size() < 5 || owns.find(' ') == std::string::npos) {
      continue;
    }
    const std::string low = ascii_lower(owns);
    bool dup = false;
    for (const auto& e : out) {
      if (ascii_lower(e) == low) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out.push_back(owns);
    }
  }
  return out;
}

std::string wave_control_strip_owns(std::string consulta, const std::vector<std::string>& owns) {
  auto squeeze = [](std::string s) {
    std::string o;
    bool sp = false;
    for (unsigned char c : s) {
      if (std::isspace(c) != 0) {
        if (!o.empty()) {
          sp = true;
        }
      } else {
        if (sp) {
          o.push_back(' ');
          sp = false;
        }
        o.push_back(static_cast<char>(c));
      }
    }
    return o;
  };
  for (const auto& o : owns) {
    const std::string needle = trim_ws_copy(o);
    if (needle.size() < 5 || needle.find(' ') == std::string::npos) {
      continue;
    }
    for (;;) {
      const auto low = ascii_lower(consulta);
      const auto nlow = ascii_lower(needle);
      const auto p = low.find(nlow);
      if (p == std::string::npos) {
        break;
      }
      consulta.erase(p, needle.size());
    }
  }
  consulta = squeeze(trim_ws_copy(consulta));
  auto strip_tail = [&](const char* t) {
    const auto low = ascii_lower(consulta);
    const std::string tail(t);
    if (low.size() >= tail.size() &&
        low.compare(low.size() - tail.size(), tail.size(), ascii_lower(tail)) == 0) {
      consulta.resize(consulta.size() - tail.size());
      consulta = trim_ws_copy(consulta);
    }
  };
  for (int i = 0; i < 3; ++i) {
    strip_tail(" en el");
    strip_tail(" en la");
    strip_tail(" en los");
    strip_tail(" en las");
    strip_tail(" del");
    strip_tail(" de la");
  }
  return squeeze(trim_ws_copy(consulta));
}

std::string wave_control_rama_nudge(const std::string& consulta) {
  std::string out = "Ese encargo mezcló dos trabajos. Atomiza: un hijo, un trozo que pueda cerrar. "
                    "Si ya hay plan, reescribe esa consulta; el resto sigue en hacia. No metas un "
                    "objeto que no estuviera en lo rechazado.";
  if (!consulta.empty()) {
    std::string q = consulta;
    if (q.size() > 220) {
      utf8_resize(&q, 220);
    }
    out += " Rechazado: " + q;
  }
  return out;
}

std::string wave_control_misma_caza_nudge(const std::string& prev, const std::string& proposed) {
  auto clip = [](std::string q) {
    if (q.size() > 220) {
      utf8_resize(&q, 220);
    }
    return q;
  };
  std::string out = "Ya se tiró esta caza: «" + clip(trim_ws_copy(prev)) +
                    "». Eso no es otro verbo. Esa pregunta está hecha. Cambia el objeto, no la "
                    "redacción.";
  if (!proposed.empty()) {
    out += " Rechazado: " + clip(trim_ws_copy(proposed));
  }
  return out;
}

std::string wave_control_cerrar_nudge() {
  return "cerrar.why cita el mecanismo que leíste y el hueco si queda. "
         "PROHIBIDO una coletilla ('el ancla queda contestada', 'lo que preguntamos y leímos').";
}

std::string wave_control_plantilla_nudge(const std::string& consulta) {
  std::string out =
      "consulta = prompt de investigación: fenómeno y verbo que el inspect huele "
      "(dónde se apaga el spinner; dónde se traduce Escape a cancelar). "
      "PROHIBIDO la plantilla 'dónde se invoca la función que…'. El hijo huele ese prompt, no el ancla.";
  if (!consulta.empty()) {
    std::string q = consulta;
    if (q.size() > 220) {
      utf8_resize(&q, 220);
    }
    out += " Rechazado: " + q;
  }
  return out;
}

std::string wave_control_system_prompt(WaveControlCue cue) {
  const bool neutral = cue == WaveControlCue::Neutral;
  std::ostringstream out;
  out << R"(Eres el PILOTO DE CONTROL. Diriges la investigación. Despiertas al llegar la consulta del usuario.
NO lees código. NO peek. Los exploradores leen; tú eliges qué cazar y con qué encargo.
El pack es el atlas de fichas M* (peek/nucleus/port). Hipótesis, no el código.
Tú decides: un explorador si UNA caza cierra el ancla, o un plan (locator / puente / seguir, 2–4 fases) si contestar exige cazas independientes y luego componerlas. El runtime lanza de uno en uno; tú juzgas puertas.
El plan sale de ESTA consulta y de lo ya visto, no de una receta. Un plan congelado no es un contrato: tras un Cerrado puedes soltarlo (explorar o un plan nuevo).
Una fase es una pregunta que un hijo puede cerrar sin el resultado de las otras. Si el mapa no une qué está junto, puedes partir. Tú eliges la rotura.
En un plan, solo el primer locator trae consulta (ataque de lo visto). Los demás: hacia; la consulta se escribe al lanzar esa fase.
consulta = prompt de investigación: fenómeno y verbo que el inspect huele (un hijo, un objeto). No una plantilla ('dónde se invoca la función que…'). Nombrar el mecanismo no es recap del ancla; recap es copiar el ancla. Compartir palabras con el ancla no es recap. why = el análisis (el hijo no la lee). El hijo no ve el ancla: su retrieval es ESA consulta.
El hijo es un cazador estrecho. Atomiza el trabajo: cada encargo es un trozo que un hijo termina sin el resto. Si metes varios trabajos en una consulta, pierde filo. Tú eliges el grano: un tirón, o un plan de cazas encapsuladas.
Si se puede copiar el ancla entero, es un recap: no la emitas. PROHIBIDO una categoría ('los archivos') o enumerar el ancla. kind=hole que solo rima con el ancla no es el objeto. Si el pack no nombra el mecanismo: amplia fichas aún cerradas, o plan de cazas; no inventes el locator desde el ancla.
hacia opcional: 1 concepto de cerca, 1–3 palabras (no ids, no paths). Un objeto; el hijo caza los símbolos. Si pegas un port largo, se recorta; no tumba la consulta.
evita opcional: 1–3 conceptos que el hijo no debe reabrir (el falso amigo de una caza que falló). El runtime no veta prompts parecidos; tú vetas.
PROHIBIDO dos exploradores a la vez. Máx 3 exploradores sueltos (los ya hechos cuentan). Si al tope falta un eslabón, plan (un locator más sobre lo visto) o cierra citando el mecanismo leído. PROHIBIDO una coletilla ('el ancla queda contestada').
PROHIBIDO copiar M*, paths, stems, owns, peek/nucleus.
Si varios barrios empatan o el pack no te deja un mecanismo, amplia 1–3 M* PRIMERO (puedes más de una vez). Luego explorar o plan.
Lo abierto llega como inspect; entre abiertas = ports, no cosine. Chrome no es el disparo.
Con 2+ trabajos el pack enseña visto, hops extra y entre abiertas (camino o sin camino). sin camino es evidencia, no un deber. No copies loci.
Si Cerrado dice que no se encontró el objeto, esa caza falló: el siguiente tiro es la clase (el sistema que lo contiene), no el mismo particular en otro barrio. Un hueco que nombra esa clase sí vale; evita lo leído. El pack (entre abiertas / extra) dice el arco, no el ancla. Si la última caza cerró: el siguiente tiro es un port que ya ves, no el ancla ni el mismo keep. 'mismo objeto' en entre abiertas es que la caza no se movió.
Un explorar es una sola rama y un solo locus. Si ves dos loci, atomiza: otro encargo o un plan; no los empaquetes en un hijo. Tú decides el grano.
)";
  if (kWaveControlBosquejoLive) {
    out << "bosquejar: 2–8 olores de zona (clase de sitio, no los nombres del ancla). Distinto de "
           "hacia: hacia apunta a un objeto; bosquejar nombra tipos de barrio para trazar el mapa. "
           "El runtime pinta barrios y aristas; no es un job. No copies la foto ni el ancla.\n";
  }
  if (kWaveControlCatalogLive) {
    out << "El plano es el territorio (barrios y stems). Llega frío: las agujas lo calibran; zoom "
           "abre resolución sin leer código; ampliar sigue siendo M*.\n"
           "agujas: 2–8 clases de sitio que cubran las zonas del ancla. Una tanda. Distinto de "
           "bosquejar y de needles de grep. Con luz, zoom; no pidas más agujas.\n"
           "zoom: 1–3 ids del plano (barrio o stem). Distinto de ampliar: ampliar abre M*; zoom "
           "abre el catálogo. No copies ids a la consulta.\n";
  }
  if (!neutral) {
    out << "El hijo cierra SU consulta. Tú cierras el ancla. Un Cerrado limpio del hijo no es el ancla.\n";
  }
  out << R"(No cierres sin haber lanzado al menos un explorador.
Con plan: el runtime usa la consulta de la fase en curso; si aún no hay, revisar la escribe.

JSON. Primer carácter `{`:
)";
  if (kWaveControlCatalogLive) {
    out << R"({"action":"control_v1","do":"agujas","agujas":["event","mouse","ai","file"],"why":"el ancla toca entrada, clic, generación y archivo; quiero ver qué barrios cubren esas zonas"}
{"action":"control_v1","do":"zoom","ids":["ui","console_panel"],"why":"quiero ver qué stems viven en el barrio de entrada"}
)";
  }
  if (kWaveControlBosquejoLive) {
    out << R"({"action":"control_v1","do":"bosquejar","hacia":["evento entrada","parada trabajo","archivo"],"why":"quiero ver si el ancla vive en un barrio o en varios"}
)";
  }
  out << R"({"action":"control_v1","do":"ampliar","ids":["M7","M8"],"why":"ui vs controlador; ov no decide el objeto"}
{"action":"control_v1","do":"explorar","consulta":"dónde se apaga el spinner de pensamiento cuando acaba la generación","why":"el inspect huele el busy de la ia; un hijo cierra el OFF"}
{"action":"control_v1","do":"plan","modo":"romper","fases":[{"id":"A","kind":"locator","consulta":"dónde se apaga el spinner de pensamiento cuando acaba la generación"},{"id":"B","kind":"locator","hacia":["parada"]},{"id":"P","kind":"puente","need":["A","B"]}],"why":"dos cierres independientes; el hijo de A no necesita B"}
{"action":"control_v1","do":"plan","modo":"seguir","fases":[{"id":"B","kind":"locator","consulta":"dónde se apaga el spinner de pensamiento cuando acaba la generación"},{"id":"S","kind":"seguir","need":["B"],"hacia":["parada"]}],"why":"una línea; tiras del flujo"}
{"action":"control_v1","do":"explorar","consulta":"quién llama al indicador de ocupado cuando acaba el trabajo","hacia":["parada"],"why":"la caza anterior cerró el OFF; el pack une a la parada"}
{"action":"control_v1","do":"explorar","consulta":"dónde se enciende el indicador de ocupado al arrancar un trabajo","evita":["formateo de string"],"why":"el particular falló; subo a la clase. el pack dice el arco"}
{"action":"control_v1","do":"pasar","why":"el Cerrado afirma el objeto de esta fase"}
{"action":"control_v1","do":"no_pasar","why":"el Cerrado niega este pico; salto a la siguiente caza"}
{"action":"control_v1","do":"revisar","consulta":"dónde se enciende el indicador de ocupado al arrancar un trabajo","evita":["formateo de string"],"why":"el particular falló; la clase, no el mismo nombre en otro barrio"}
{"action":"control_v1","do":"cerrar","why":"el indicador de ocupado se apaga al terminar el trabajo; si no, el busy queda preso"}
)";
  return out.str();
}

std::string wave_control_user_prompt(const std::string& user_consulta, const std::string& jobs_md,
                                     const std::string& barrio_brief, const std::string& inspect_brief,
                                     const WaveControlPlan& plan,
                                     const std::vector<WaveControlDo>& legal_in,
                                     const std::string& circuit_md, const std::string& exam_md,
                                     WaveControlCue cue, const std::string& bosquejo_md,
                                     const std::string& plano_md, const std::string& zoom_md) {
  (void)cue;
  std::ostringstream out;
  const bool inspect_first = !inspect_brief.empty();
  if (!inspect_first) {
    out << "Consulta del usuario (ancla):\n" << user_consulta << "\n\n";
  }
  if (kWaveControlCatalogLive) {
    if (!plano_md.empty()) {
      out << plano_md;
      if (plano_md.back() != '\n') {
        out << "\n";
      }
      out << "\n";
    }
    if (!zoom_md.empty()) {
      out << zoom_md;
      if (zoom_md.back() != '\n') {
        out << "\n";
      }
      out << "\n";
    }
  }
  if (!bosquejo_md.empty()) {
    out << bosquejo_md;
    if (bosquejo_md.back() != '\n') {
      out << "\n";
    }
    out << "\n";
  }
  if (!barrio_brief.empty()) {
    out << barrio_brief << "\n\n";
  }
  if (!inspect_brief.empty()) {
    out << inspect_brief << "\n\n";
  }
  const std::string board = wave_control_plan_markdown(plan);
  if (!board.empty()) {
    out << board << "\n";
  }
  out << "Trabajos ya hechos:\n";
  out << (jobs_md.empty() ? "(ninguno)\n" : jobs_md);
  if (!circuit_md.empty()) {
    out << "\n" << circuit_md;
    if (circuit_md.back() != '\n') {
      out << "\n";
    }
  }
  if (!exam_md.empty()) {
    out << "\n" << exam_md;
    if (exam_md.back() != '\n') {
      out << "\n";
    }
  }
  const int jobs_n = jobs_md.empty() ? 0 : 1;
  const bool last_visto = jobs_md.find("Cerrado:") != std::string::npos &&
                          jobs_md.find("(vacío)") == std::string::npos;
  const auto legal =
      legal_in.empty() ? wave_control_legal(plan, jobs_n, last_visto) : legal_in;
  out << "\n" << wave_control_legal_markdown(legal);
  if (!plan.committed && jobs_md.empty() && inspect_brief.empty()) {
    if (kWaveControlCatalogLive && !zoom_md.empty()) {
      out << "Ya hay zoom. Tú eliges: un explorar (un cazador; puede cubrir todo el ancla) "
             "o un plan (partir solo si ves cazas independientes). No partas porque el plano "
             "muestre varios barrios. PROHIBIDO copiar ids.\n";
    } else if (kWaveControlCatalogLive && plano_md.find("luz (") != std::string::npos) {
      out << "El plano ya tiene luz. zoom a un barrio o stem caliente. PROHIBIDO otra tanda de "
             "agujas. PROHIBIDO copiar ids.\n";
    } else if (kWaveControlCatalogLive) {
      out << "Aún no hay exploradores. El plano llega frío: primero agujas que cubran las zonas del "
             "ancla (entrada, generación, archivo), no idents del prompt. "
             "Luego zoom a un barrio o stem. Después TÚ eliges: un explorar (puede cubrir todo el "
             "ancla) o un plan (solo si ves cazas independientes). Si ov empatado, amplia 1–3 M*. "
             "PROHIBIDO copiar ids.\n";
    } else {
      out << "Aún no hay exploradores. El pack son las fichas. Si ov empatado o no hueles un "
             "mecanismo en owns/núcleo, amplia 1–3 M*. No copies el ancla: aún no hay inspect. "
             "El prompt de investigación sale de lo visto. Si el ancla enumera, la consulta "
             "no enumera. PROHIBIDO copiar ids.\n";
    }
  } else if (!plan.committed && jobs_md.empty()) {
    out << "Abiertas están completas (inspect). entre abiertas son ports, no cosine. "
           "Atomiza: cada encargo es un trozo que un hijo cierra. Si el ancla enumera dos disparos, "
           "la consulta nombra UNO de lo visto. Si cabe en un tirón, explorar: "
           "consulta = prompt de investigación de ESTE inspect (fenómeno y verbo), no una plantilla "
           "('invoca la función que…') ni el ancla copiado. Nombrar el mecanismo no es recap; "
           "compartir palabras con el ancla tampoco. Si el "
           "trabajo cruza fronteras que un hijo no une, plan: el primer locator es el ataque de ESTE "
           "inspect; los siguientes solo hacia (la consulta se escribe al lanzar). why = por qué ese "
           "grano; el hijo no la lee. hacia opcional: 1–3 palabras, no el texto de un port (si es "
           "largo se recorta). Ampliar solo ids que aún no estén "
           "abiertos. Owns/peek no van en la consulta. Chrome no es el disparo. kind=hole que solo "
           "rima con el ancla no es el objeto. PROHIBIDO copiar ids.\n";
  } else if (plan.committed) {
    const bool last_fail = control_jobs_last_failed(jobs_md);
    const std::string last_c = control_jobs_last_cerrado(jobs_md);
    const bool last_ok =
        !last_c.empty() && last_c != "(vacío)" && !last_fail;
    out << "El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del "
           "pack, o un plan nuevo.\n";
    const int cur = wave_control_plan_current(plan);
    if (cur >= 0) {
      const auto& ph = plan.fases[static_cast<std::size_t>(cur)];
      if (ph.status == WaveControlPhaseStatus::EnCurso &&
          ph.kind == WaveControlPhaseKind::Locator && ph.consulta.empty()) {
        out << "Esta fase no tiene briefing. revisar: consulta = ataque de lo visto (un locus), "
               "no el ancla. O suelta el plan. PROHIBIDO copiar ids.\n";
      } else if (ph.status == WaveControlPhaseStatus::EnCurso) {
        out << "Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un "
               "visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes "
               "otro hijo en el mismo pico). ";
        if (last_fail) {
          out << "Si el particular falló, revisar es la clase, no el mismo nombre en otro archivo. "
                 "Un hueco de clase sí vale. evita lo leído. El pack dice el arco. ";
        } else if (last_ok) {
          out << "La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / "
                 "extra), no el ancla ni el mismo keep. ";
        }
        out << "PROHIBIDO copiar ids.\n";
      } else if (ph.status == WaveControlPhaseStatus::Fallo) {
        out << "Esta fase falló. suelta el plan o cierra. PROHIBIDO copiar ids.\n";
      } else {
        out << "Lee Cerrado. Cierra si el ancla ya se responde.\n";
      }
    } else {
      out << "Lee Cerrado. Cierra si el ancla ya se responde, o suelta el plan y sigue un port.\n";
    }
  } else if (!jobs_md.empty()) {
    const bool can_explore = wave_control_do_allowed(legal, WaveControlDo::Explorar);
    const bool can_plan = wave_control_do_allowed(legal, WaveControlDo::Plan);
    if (!can_explore && can_plan) {
      out << "explorar agotado. Si falta un eslabón, plan (un locator más sobre lo visto; la "
             "consulta se escribe al lanzar) o cierra citando el mecanismo leído. PROHIBIDO una "
             "coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.\n";
    } else {
      const bool last_fail = control_jobs_last_failed(jobs_md);
      const std::string last_c = control_jobs_last_cerrado(jobs_md);
      const bool last_ok =
          !last_c.empty() && last_c != "(vacío)" && !last_fail;
      out << "Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). "
             "El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). ";
      if (last_fail) {
        out << "Si el Cerrado dice que no se encontró el objeto, esa caza falló: el siguiente "
               "tiro es la clase (el sistema que lo contiene), no el mismo nombre en otro barrio. "
               "Un hueco que nombra esa clase sí vale; evita lo leído. El pack (entre abiertas / "
               "extra) dice el arco, no el ancla. ";
      } else if (last_ok) {
        out << "La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / "
               "extra), no el ancla ni el mismo keep. ";
      }
      out << "'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un "
             "tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación "
             "(fenómeno y verbo; el hijo no ve el ancla); "
             "why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: "
             "conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no "
             "empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.\n";
    }
  } else {
    out << "Lee Cerrado y leído. Si Cerrado cubre el ancla y Abierto está vacío, cierra. "
           "Abierto vacío no es un deber. Otro explorar solo si Abierto nombra un objeto "
           "que no está en leído. PROHIBIDO copiar ids.\n";
  }
  if (inspect_first) {
    out << "Cierras esto (no lo recopies):\n" << user_consulta << "\n";
  }
  out << "JSON ahora. Primer carácter `{`.\n";
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
  cap << "\n(El why no es evidencia de lo no leído.)\n";
  if (!st->papeles.empty()) {
    const auto miss = wave_guion_uncovered(*st);
    cap << "Guion:";
    for (const auto& p : st->papeles) {
      const bool abierta = std::find(miss.begin(), miss.end(), p) != miss.end();
      cap << " `" << p << "`" << (abierta ? " [sin evidencia]" : " [con evidencia]");
    }
    cap << "\n";
    if (!miss.empty()) {
      cap << "Preguntas sin evidencia:";
      for (const auto& p : miss) {
        cap << " `" << p << "`";
      }
      cap << "\n(Afirmar un papel no leído no es evidencia.)\n";
    }
  }
  cap << "\n";
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
  nlohmann::json cerca_log = nlohmann::json::array();
  for (const auto& rec : st.cerca_log) {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& row : rec.rows) {
      rows.push_back({{"id", row.id},
                      {"path", row.path},
                      {"symbol", row.symbol},
                      {"stem", row.stem},
                      {"kind", row.kind},
                      {"cosine", row.cosine},
                      {"hop", row.hop},
                      {"card", row.card},
                      {"already_read", row.already_read}});
    }
    cerca_log.push_back({{"needles", rec.needles},
                         {"in", rec.in_scopes},
                         {"query", rec.query},
                         {"hops", rec.hops},
                         {"hits", rec.hits},
                         {"added", rec.added},
                         {"wake", rec.wake_note},
                         {"rows", rows}});
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
  auto hops_json = [](const std::vector<WavePeekHop>& hops) {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& h : hops) {
      a.push_back({{"loc", h.loc},
                   {"stem", h.stem},
                   {"hops", h.hops},
                   {"cosine", h.cosine}});
    }
    return a;
  };
  for (const auto& n : st.peek_neighbors) {
    neigh.push_back({{"loc", n.loc},
                     {"callers", hops_json(n.callers)},
                     {"callees", hops_json(n.callees)},
                     {"export_loc", n.export_loc},
                     {"export_callers", hops_json(n.export_callers)}});
  }
  nlohmann::json sketch = nlohmann::json::array();
  for (const auto& e : wave_sketch_edges(st)) {
    sketch.push_back({{"from", e.from}, {"to", e.to}, {"via", e.via}});
  }
  std::string atlas_query;
  int atlas_zones = 0;
  if (st.atlas_cards.is_object()) {
    atlas_query = st.atlas_cards.value("query", "");
    if (st.atlas_cards.contains("zones") && st.atlas_cards["zones"].is_array()) {
      atlas_zones = static_cast<int>(st.atlas_cards["zones"].size());
    }
  }
  return {{"prompt", st.prompt},
          {"campo", st.campo},
          {"papeles", st.papeles},
          {"independiente_done", st.independiente_done},
          {"independiente_leaf", st.independiente_leaf},
          {"control_worker", st.control_worker},
          {"pin_from", st.pin_from},
          {"pin_to", st.pin_to},
          {"pin_loci", st.pin_loci},
          {"pin_hacia", st.pin_hacia},
          {"pin_evita", st.pin_evita},
          {"pin_fallo_md", st.pin_fallo_md},
          {"cerca_hops_max", st.cerca_hops_max},
          {"peek_hop_depth", st.peek_hop_depth},
          {"atlas_md", st.atlas_md},
          {"atlas_query", atlas_query},
          {"atlas_zones", atlas_zones},
          {"opened_md", st.opened_md},
          {"opened_ids", st.opened_ids},
          {"candidatas", cands},
          {"needles_log", needles_log},
          {"cerca_log", cerca_log},
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
          {"in_scopes", ola.in_scopes},
          {"hops", ola.hops},
          {"why", ola.why},
          {"huecos", ola.huecos},
          {"papeles", ola.papeles},
          {"papel", ola.papel}};
}

}  // namespace tuide
