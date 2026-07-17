#include "dap/gdb_protocol.hpp"

namespace dap {

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(GdbLaunchRequest, LaunchRequest, "launch",
                                  DAP_FIELD(program, "program"),
                                  DAP_FIELD(cwd, "cwd"), DAP_FIELD(args, "args"),
                                  DAP_FIELD(stopAtBeginningOfMainSubprogram,
                                            "stopAtBeginningOfMainSubprogram"));

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(BashdbLaunchRequest, LaunchRequest, "launch",
                                  DAP_FIELD(program, "program"),
                                  DAP_FIELD(cwd, "cwd"), DAP_FIELD(args, "args"),
                                  DAP_FIELD(pathBash, "pathBash"),
                                  DAP_FIELD(pathBashdb, "pathBashdb"),
                                  DAP_FIELD(pathBashdbLib, "pathBashdbLib"),
                                  DAP_FIELD(pathCat, "pathCat"),
                                  DAP_FIELD(pathMkfifo, "pathMkfifo"),
                                  DAP_FIELD(pathPkill, "pathPkill"),
                                  DAP_FIELD(terminalKind, "terminalKind"));

DAP_IMPLEMENT_STRUCT_TYPEINFO_EXT(GdbAttachRequest, AttachRequest, "attach",
                                  DAP_FIELD(program, "program"),
                                  DAP_FIELD(pid, "pid"),
                                  DAP_FIELD(target, "target"),
                                  DAP_FIELD(coreFile, "coreFile"));

DAP_IMPLEMENT_STRUCT_TYPEINFO(GdbTerminatedEvent, "terminated");

}  // namespace dap