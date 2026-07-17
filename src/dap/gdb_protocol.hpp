#pragma once

#include "dap/protocol.h"
#include "dap/typeof.h"

namespace dap {

struct GdbLaunchRequest : public LaunchRequest {
  optional<string> program;
  optional<string> cwd;
  optional<array<string>> args;
  optional<boolean> stopAtBeginningOfMainSubprogram;
  optional<boolean> stopOnEntry;  // debugpy / generic DAP
  optional<string> console;       // debugpy: internalConsole | integratedTerminal | ...
};

DAP_DECLARE_STRUCT_TYPEINFO(GdbLaunchRequest);

struct BashdbLaunchRequest : public LaunchRequest {
  optional<string> program;
  optional<string> cwd;
  optional<array<string>> args;
  optional<string> argsString;
  optional<object> env;
  optional<string> pathBash;
  optional<string> pathBashdb;
  optional<string> pathBashdbLib;
  optional<string> pathCat;
  optional<string> pathMkfifo;
  optional<string> pathPkill;
  optional<string> terminalKind;
};

DAP_DECLARE_STRUCT_TYPEINFO(BashdbLaunchRequest);

struct GdbAttachRequest : public AttachRequest {
  optional<string> program;
  optional<integer> pid;
  optional<string> target;
  optional<string> coreFile;
};

DAP_DECLARE_STRUCT_TYPEINFO(GdbAttachRequest);

// GDB suele emitir "terminated" sin body; el TerminatedEvent estándar de cppdap
// falla al deserializar el campo opcional restart desde un body ausente.
struct GdbTerminatedEvent : public Event {};

DAP_DECLARE_STRUCT_TYPEINFO(GdbTerminatedEvent);

// debugpy custom events (ignored by the client; must be registered so cppdap
// does not report "No event handler registered").
struct DebugpyWaitingForServerEvent : public Event {
  optional<string> host;
  optional<integer> port;
};

DAP_DECLARE_STRUCT_TYPEINFO(DebugpyWaitingForServerEvent);

}  // namespace dap
