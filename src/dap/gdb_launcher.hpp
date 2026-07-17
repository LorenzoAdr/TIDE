#pragma once

#include <memory>
#include <string>

#include "dap/io.h"

namespace tgdb {

bool gdb_supports_dap_at(const std::string& gdb_path);
bool gdb_supports_dap();
bool debugpy_available();
bool bashdb_dap_available();

class GdbProcess {
 public:
  GdbProcess();
  ~GdbProcess();

  GdbProcess(const GdbProcess&) = delete;
  GdbProcess& operator=(const GdbProcess&) = delete;

  bool start();
  void stop(bool force = false);

  std::shared_ptr<dap::Reader> reader() const { return reader_; }
  std::shared_ptr<dap::Writer> writer() const { return writer_; }
  bool running() const { return running_; }

 private:
  int child_pid_ = -1;
  int stdin_write_fd_ = -1;
  int stdout_read_fd_ = -1;
  std::shared_ptr<dap::Reader> reader_;
  std::shared_ptr<dap::Writer> writer_;
  bool running_ = false;
};

}  // namespace tgdb
