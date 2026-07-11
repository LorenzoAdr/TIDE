#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgdb {

struct SemanticTokenSpan {
  int start_col = 0;
  int length = 0;
  int type = 0;
  int modifiers = 0;
};

struct SemanticTokenDocument {
  std::vector<std::string> token_types;
  std::vector<std::vector<SemanticTokenSpan>> lines;
  bool ready = false;
  uint64_t source_generation = 0;
  // Server-assigned id for the response this document was decoded from. Sent back as
  // `previousResultId` on the next `textDocument/semanticTokens/full/delta` request so
  // clangd can reply with just the edits instead of re-sending the whole file's tokens.
  std::string result_id;
  // Flat token data backing `lines` (5 ints per token: deltaLine, deltaStart, length,
  // tokenType, tokenModifiers), kept around so a future delta response's edits can be
  // spliced into it and re-decoded without needing a fresh full fetch.
  std::vector<int64_t> raw_data;
};

// Content hash of a single line's semantic token spans. Used as a per-line cache-key
// ingredient instead of the document-wide semantic token revision counter: a full
// clangd re-fetch bumps that revision for the whole file even when the vast majority
// of lines' tokens didn't actually change, which used to force every visible line to
// be re-rendered. Hashing the actual span content means a line whose tokens are
// byte-for-byte identical across two fetches keeps the same key and stays cached.
inline uint64_t hash_semantic_token_line(const std::vector<SemanticTokenSpan>* spans) {
  uint64_t h = 1469598103934665603ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  if (spans == nullptr) {
    return h;
  }
  for (const SemanticTokenSpan& span : *spans) {
    h = (h ^ static_cast<uint64_t>(span.start_col)) * kPrime;
    h = (h ^ static_cast<uint64_t>(span.length)) * kPrime;
    h = (h ^ static_cast<uint64_t>(span.type)) * kPrime;
    h = (h ^ static_cast<uint64_t>(span.modifiers)) * kPrime;
  }
  return h;
}

}  // namespace tgdb
