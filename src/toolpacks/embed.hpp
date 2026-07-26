#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "toolpacks/store.hpp"

namespace tuide::toolpacks {

inline constexpr const char* kEmbedMagicStr = "TUIDTPK1";  // exactly 8 bytes

struct EmbeddedToolpackInfo {
  std::string id;
  std::string version;
  std::uint64_t blob_offset = 0;
  std::uint64_t blob_size = 0;
};

struct EmbedIndex {
  std::uint64_t trailer_offset = 0;
  std::vector<EmbeddedToolpackInfo> entries;
};

std::optional<EmbedIndex> read_embed_index(const std::string& binary_path);
bool binary_has_toolpack_embed(const std::string& binary_path);

std::string extract_embedded_toolpack(const std::string& binary_path,
                                      const EmbeddedToolpackInfo& entry,
                                      const std::string& dest_dir);

std::optional<ResolvedToolpack> resolve_embedded_clangd_toolpack();

// Append trailer with pre-built zstd blobs to an output file that is a clean ELF copy.
// blobs: parallel arrays of id, version, path-to-.tar.zst
std::string append_toolpack_trailer(const std::string& output_binary_path,
                                    const std::vector<std::string>& ids,
                                    const std::vector<std::string>& versions,
                                    const std::vector<std::string>& blob_paths);

}  // namespace tuide::toolpacks
