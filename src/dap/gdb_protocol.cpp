#include "dap/gdb_protocol.hpp"

namespace dap {

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(GdbLaunchRequest, LaunchRequest, "launch",
                                  DAP_FIELD(program, "program"),
                                  DAP_FIELD(cwd, "cwd"), DAP_FIELD(args, "args"),
                                  DAP_FIELD(stopAtBeginningOfMainSubprogram,
                                            "stopAtBeginningOfMainSubprogram"));

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(GdbAttachRequest, AttachRequest, "attach",
                                  DAP_FIELD(program, "program"),
                                  DAP_FIELD(pid, "pid"),
                                  DAP_FIELD(target, "target"));

}  // namespace dap
