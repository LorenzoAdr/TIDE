#include "packet_monitor/pkt_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace tuide::packet_monitor {

namespace fs = std::filesystem;

namespace {

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

std::size_t field_size(FieldType type) {
  switch (type) {
    case FieldType::kU8:
    case FieldType::kI8:
    case FieldType::kBytes:
      return 1;
    case FieldType::kU16:
    case FieldType::kI16:
      return 2;
    case FieldType::kU32:
    case FieldType::kI32:
    case FieldType::kF32:
      return 4;
    case FieldType::kF64:
      return 8;
  }
  return 1;
}

bool parse_field_type(const std::string& text, FieldType* out, std::size_t* array_count) {
  if (out == nullptr) {
    return false;
  }
  std::size_t count = 0;
  std::string base = text;
  const auto bracket = text.find('[');
  if (bracket != std::string::npos) {
    base = text.substr(0, bracket);
    const auto end = text.find(']', bracket);
    if (end == std::string::npos) {
      return false;
    }
    try {
      count = static_cast<std::size_t>(std::stoul(text.substr(bracket + 1, end - bracket - 1)));
    } catch (...) {
      return false;
    }
  }
  const std::string lowered = to_lower(base);
  if (lowered == "u8") {
    *out = FieldType::kU8;
  } else if (lowered == "i8") {
    *out = FieldType::kI8;
  } else if (lowered == "u16") {
    *out = FieldType::kU16;
  } else if (lowered == "i16") {
    *out = FieldType::kI16;
  } else if (lowered == "u32") {
    *out = FieldType::kU32;
  } else if (lowered == "i32") {
    *out = FieldType::kI32;
  } else if (lowered == "f32" || lowered == "float") {
    *out = FieldType::kF32;
  } else if (lowered == "f64" || lowered == "double") {
    *out = FieldType::kF64;
  } else if (lowered == "bytes") {
    *out = FieldType::kBytes;
  } else {
    return false;
  }
  if (array_count != nullptr) {
    *array_count = count;
  }
  return true;
}

Endianness parse_endian(const std::string& text) {
  if (to_lower(text) == "big") {
    return Endianness::kBig;
  }
  return Endianness::kLittle;
}

uint16_t read_u16(const std::vector<uint8_t>& data, std::size_t offset, Endianness endian) {
  if (offset + 1 >= data.size()) {
    return 0;
  }
  uint16_t raw = static_cast<uint16_t>(data[offset]) |
                 (static_cast<uint16_t>(data[offset + 1]) << 8);
  if (endian == Endianness::kBig) {
    raw = static_cast<uint16_t>((raw << 8) | (raw >> 8));
  }
  return raw;
}

uint32_t read_u32(const std::vector<uint8_t>& data, std::size_t offset, Endianness endian) {
  if (offset + 3 >= data.size()) {
    return 0;
  }
  uint32_t raw = static_cast<uint32_t>(data[offset]) |
                 (static_cast<uint32_t>(data[offset + 1]) << 8) |
                 (static_cast<uint32_t>(data[offset + 2]) << 16) |
                 (static_cast<uint32_t>(data[offset + 3]) << 24);
  if (endian == Endianness::kBig) {
    raw = ((raw & 0x000000FFu) << 24) | ((raw & 0x0000FF00u) << 8) |
          ((raw & 0x00FF0000u) >> 8) | ((raw & 0xFF000000u) >> 24);
  }
  return raw;
}

float read_f32(const std::vector<uint8_t>& data, std::size_t offset, Endianness endian) {
  const uint32_t bits = read_u32(data, offset, endian);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double read_f64(const std::vector<uint8_t>& data, std::size_t offset, Endianness endian) {
  if (offset + 7 >= data.size()) {
    return 0.0;
  }
  uint64_t raw = 0;
  for (int i = 0; i < 8; ++i) {
    raw |= static_cast<uint64_t>(data[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  if (endian == Endianness::kBig) {
    raw = ((raw & 0x00000000000000FFull) << 56) | ((raw & 0x000000000000FF00ull) << 40) |
          ((raw & 0x0000000000FF0000ull) << 24) | ((raw & 0x00000000FF000000ull) << 8) |
          ((raw & 0x000000FF00000000ull) >> 8) | ((raw & 0x0000FF0000000000ull) >> 24) |
          ((raw & 0x00FF000000000000ull) >> 40) | ((raw & 0xFF00000000000000ull) >> 56);
  }
  double value = 0.0;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

std::string format_hex_byte(uint8_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
         << static_cast<int>(value);
  return stream.str();
}

bool parse_variant(const std::string& key, const nlohmann::json& node, VariantDefinition* out) {
  if (out == nullptr || !node.is_object()) {
    return false;
  }
  out->key = key;
  if (node.contains("name") && node["name"].is_string()) {
    out->name = node["name"].get<std::string>();
  } else {
    out->name = key;
  }
  if (node.contains("min_length") && node["min_length"].is_number_unsigned()) {
    out->min_length = node["min_length"].get<std::size_t>();
  }
  if (!node.contains("fields") || !node["fields"].is_array()) {
    return true;
  }
  for (const auto& field_node : node["fields"]) {
    if (!field_node.is_object() || !field_node.contains("name") || !field_node.contains("type") ||
        !field_node.contains("offset")) {
      continue;
    }
    FieldDefinition field;
    field.name = field_node["name"].get<std::string>();
    field.offset = field_node["offset"].get<std::size_t>();
    std::size_t array_count = 0;
    if (!parse_field_type(field_node["type"].get<std::string>(), &field.type, &array_count)) {
      continue;
    }
    field.array_count = array_count;
    out->fields.push_back(std::move(field));
  }
  return true;
}

std::string decode_field_value(const FieldDefinition& field, const std::vector<uint8_t>& payload,
                               Endianness endian) {
  if (field.type == FieldType::kBytes) {
    const std::size_t count = field.array_count == 0 ? 1 : field.array_count;
    std::ostringstream stream;
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t offset = field.offset + i;
      if (offset >= payload.size()) {
        break;
      }
      if (i > 0) {
        stream << ' ';
      }
      stream << format_hex_byte(payload[offset]);
    }
    return stream.str();
  }

  std::ostringstream stream;
  const std::size_t count = field.array_count == 0 ? 1 : field.array_count;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset = field.offset + i * field_size(field.type);
    if (offset >= payload.size()) {
      break;
    }
    if (i > 0) {
      stream << ", ";
    }
    switch (field.type) {
      case FieldType::kU8:
        stream << static_cast<unsigned>(payload[offset]);
        break;
      case FieldType::kI8:
        stream << static_cast<int>(static_cast<int8_t>(payload[offset]));
        break;
      case FieldType::kU16:
        stream << read_u16(payload, offset, endian);
        break;
      case FieldType::kI16:
        stream << static_cast<int>(static_cast<int16_t>(read_u16(payload, offset, endian)));
        break;
      case FieldType::kU32:
        stream << read_u32(payload, offset, endian);
        break;
      case FieldType::kI32:
        stream << static_cast<int32_t>(read_u32(payload, offset, endian));
        break;
      case FieldType::kF32:
        stream << read_f32(payload, offset, endian);
        break;
      case FieldType::kF64:
        stream << read_f64(payload, offset, endian);
        break;
      default:
        break;
    }
  }
  return stream.str();
}

}  // namespace

bool load_protocol_from_json(const std::string& path, ProtocolDefinition* out, std::string* error) {
  if (out == nullptr) {
    return false;
  }
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "cannot open protocol file";
    }
    return false;
  }

  nlohmann::json doc;
  try {
    input >> doc;
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = ex.what();
    }
    return false;
  }

  ProtocolDefinition protocol;
  if (doc.contains("name") && doc["name"].is_string()) {
    protocol.name = doc["name"].get<std::string>();
  }
  if (doc.contains("endian") && doc["endian"].is_string()) {
    protocol.endian = parse_endian(doc["endian"].get<std::string>());
  }

  if (doc.contains("discriminator") && doc["discriminator"].is_object()) {
    const auto& disc = doc["discriminator"];
    protocol.has_discriminator = true;
    if (disc.contains("offset") && disc["offset"].is_number_unsigned()) {
      protocol.discriminator.offset = disc["offset"].get<std::size_t>();
    }
    if (disc.contains("type") && disc["type"].is_string()) {
      std::size_t ignored = 0;
      parse_field_type(disc["type"].get<std::string>(), &protocol.discriminator.type, &ignored);
    }
    if (disc.contains("mask") && disc["mask"].is_string()) {
      protocol.discriminator.mask = disc["mask"].get<std::string>();
    }
  }

  if (doc.contains("variants") && doc["variants"].is_object()) {
    for (const auto& [key, value] : doc["variants"].items()) {
      VariantDefinition variant;
      if (parse_variant(key, value, &variant)) {
        protocol.variants.emplace(key, std::move(variant));
      }
    }
  }

  if (doc.contains("fields") && doc["fields"].is_array()) {
    VariantDefinition variant;
    variant.name = protocol.name.empty() ? "default" : protocol.name;
    variant.key = "default";
    nlohmann::json wrapper;
    wrapper["name"] = variant.name;
    wrapper["fields"] = doc["fields"];
    if (doc.contains("min_length") && doc["min_length"].is_number_unsigned()) {
      wrapper["min_length"] = doc["min_length"];
    }
    parse_variant("default", wrapper, &variant);
    protocol.default_variant = std::move(variant);
    protocol.has_default_variant = true;
  }

  if (protocol.variants.empty() && !protocol.has_default_variant) {
    if (error != nullptr) {
      *error = "protocol has no fields or variants";
    }
    return false;
  }

  *out = std::move(protocol);
  return true;
}

std::vector<std::string> list_protocol_files(const std::string& workspace_root) {
  std::vector<std::string> files;
  if (workspace_root.empty()) {
    return files;
  }
  std::error_code ec;
  const fs::path primary = fs::path(workspace_root) / ".tuide" / "protocols";
  const fs::path fallback = fs::path(workspace_root) / "examples" / "protocols";
  const fs::path dir = fs::exists(primary, ec) ? primary : fallback;
  if (!fs::exists(dir, ec)) {
    return files;
  }
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec || !entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool read_discriminator(const std::vector<uint8_t>& payload,
                        const DiscriminatorDefinition& discriminator, std::string* out_key) {
  if (out_key == nullptr || discriminator.offset >= payload.size()) {
    return false;
  }
  std::ostringstream stream;
  switch (discriminator.type) {
    case FieldType::kU8:
      stream << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
             << static_cast<int>(payload[discriminator.offset]);
      break;
    case FieldType::kU16: {
      const uint16_t value = read_u16(payload, discriminator.offset, Endianness::kLittle);
      stream << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value;
      break;
    }
    case FieldType::kU32: {
      const uint32_t value = read_u32(payload, discriminator.offset, Endianness::kLittle);
      stream << "0x" << std::hex << std::uppercase << value;
      break;
    }
    default:
      return false;
  }
  *out_key = stream.str();
  return true;
}

DecodedPacket decode_packet(const std::vector<uint8_t>& payload, const ProtocolDefinition& protocol,
                            const std::string& variant_key) {
  DecodedPacket decoded;
  const VariantDefinition* variant = nullptr;

  std::string resolved_key = variant_key;
  if (resolved_key.empty() && protocol.has_discriminator) {
    if (!read_discriminator(payload, protocol.discriminator, &resolved_key)) {
      decoded.error = "cannot read discriminator";
      return decoded;
    }
  }

  if (!resolved_key.empty()) {
    const auto it = protocol.variants.find(resolved_key);
    if (it != protocol.variants.end()) {
      variant = &it->second;
      decoded.variant_key = resolved_key;
      decoded.variant_name = it->second.name;
    }
  }
  if (variant == nullptr && protocol.has_default_variant) {
    variant = &protocol.default_variant;
    decoded.variant_key = protocol.default_variant.key;
    decoded.variant_name = protocol.default_variant.name;
  }
  if (variant == nullptr) {
    decoded.error = "unknown packet variant";
    return decoded;
  }
  if (payload.size() < variant->min_length) {
    decoded.error = "payload too short";
    return decoded;
  }

  for (const auto& field : variant->fields) {
    DecodedField out;
    out.name = field.name;
    out.type = "field";
    out.value = decode_field_value(field, payload, protocol.endian);
    decoded.fields.push_back(std::move(out));
  }
  return decoded;
}

}  // namespace tuide::packet_monitor
