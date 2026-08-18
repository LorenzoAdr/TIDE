#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tuide {

// Official NDJSON trace for AI levels (L0/L1/L2 + embed + tools).
// Default path: <workspace>/.tuide/ai/trace.ndjson
enum class AiTraceChannel : uint8_t {
  System = 0,
  L0 = 1,
  L1 = 2,
  L2 = 3,
  Embed = 4,
  Tool = 5,
};

const char* ai_trace_channel_name(AiTraceChannel ch);

// Escape for embedding strings into JSON string values.
std::string ai_trace_escape(std::string_view s, std::size_t max_len = 2000);

// workspace_root empty → disable file writes until configured.
// path_override empty → <workspace>/.tuide/ai/trace.ndjson
void ai_trace_configure(bool enabled, std::string workspace_root,
                        std::string path_override = {});

bool ai_trace_enabled();
std::string ai_trace_path();
std::string ai_trace_status_text();

// Append one NDJSON event. data_json must be a JSON object literal (or empty → {}).
void ai_trace(AiTraceChannel channel, std::string_view event,
              std::string_view data_json = "{}");

bool ai_trace_clear(std::string* error = nullptr);
// Last N lines of the trace file (for /trace tail).
std::string ai_trace_tail(std::size_t max_lines = 40);

}  // namespace tuide
