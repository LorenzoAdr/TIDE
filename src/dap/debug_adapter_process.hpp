#pragma once

#include <memory>
#include <string>

#include "dap/debug_adapter_spec.hpp"
#include "dap/io.h"

namespace tgdb {

// Stdio DAP adapter process (gdb -i=dap, debugpy.adapter, ...).
class IDebugAdapterProcess {
 public:
  virtual ~IDebugAdapterProcess() = default;

  virtual bool start() = 0;
  virtual void stop(bool force = false) = 0;
  virtual std::shared_ptr<dap::Reader> reader() const = 0;
  virtual std::shared_ptr<dap::Writer> writer() const = 0;
  virtual bool running() const = 0;
  virtual DebugAdapterKind kind() const = 0;
  virtual const std::string& adapter_id() const = 0;
};

std::unique_ptr<IDebugAdapterProcess> create_debug_adapter_process(DebugAdapterKind kind);
std::unique_ptr<IDebugAdapterProcess> create_debug_adapter_process_for_program(
    const std::string& program_path);

}  // namespace tgdb
