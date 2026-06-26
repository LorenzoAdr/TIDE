#pragma once

#include "dap/protocol.h"
#include "dap/typeof.h"

namespace dap {

struct GdbLaunchRequest : public LaunchRequest {
  optional<string> program;
  optional<string> cwd;
  optional<array<string>> args;
  optional<boolean> stopAtBeginningOfMainSubprogram;
};

DAP_DECLARE_STRUCT_TYPEINFO(GdbLaunchRequest);

struct GdbAttachRequest : public AttachRequest {
  optional<string> program;
  optional<integer> pid;
  optional<string> target;
};

DAP_DECLARE_STRUCT_TYPEINFO(GdbAttachRequest);

}  // namespace dap
