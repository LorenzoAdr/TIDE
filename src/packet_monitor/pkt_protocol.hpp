#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tgdb::packet_monitor {

enum class FieldType {
  kU8,
  kI8,
  kU16,
  kI16,
  kU32,
  kI32,
  kF32,
  kF64,
  kBytes,
};

enum class Endianness { kLittle, kBig };

struct FieldDefinition {
  std::string name;
  std::size_t offset = 0;
  FieldType type = FieldType::kU8;
  std::size_t array_count = 0;
};

struct DiscriminatorDefinition {
  std::size_t offset = 0;
  FieldType type = FieldType::kU8;
  std::string mask;
};

struct VariantDefinition {
  std::string key;
  std::string name;
  std::size_t min_length = 0;
  std::vector<FieldDefinition> fields;
};

struct ProtocolDefinition {
  std::string name;
  Endianness endian = Endianness::kLittle;
  bool has_discriminator = false;
  DiscriminatorDefinition discriminator;
  std::map<std::string, VariantDefinition> variants;
  VariantDefinition default_variant;
  bool has_default_variant = false;
};

struct DecodedField {
  std::string name;
  std::string value;
  std::string type;
};

struct DecodedPacket {
  std::string variant_name;
  std::string variant_key;
  std::vector<DecodedField> fields;
  std::string error;
};

bool load_protocol_from_json(const std::string& path, ProtocolDefinition* out,
                             std::string* error = nullptr);
std::vector<std::string> list_protocol_files(const std::string& workspace_root);
bool read_discriminator(const std::vector<uint8_t>& payload,
                        const DiscriminatorDefinition& discriminator, std::string* out_key);
DecodedPacket decode_packet(const std::vector<uint8_t>& payload,
                            const ProtocolDefinition& protocol,
                            const std::string& variant_key = "");

}  // namespace tgdb::packet_monitor
