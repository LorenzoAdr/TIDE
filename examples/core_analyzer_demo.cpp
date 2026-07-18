// Demo post-mortem para Core Analyzer (tuide).
//
// Escenario:
//   - Crash en ConsoleSink::on_event (vtable de IEventSink).
//   - MetricStore vive en el heap; no aparece en el backtrace.
//   - ConsoleSink::metrics y EventRouter::store apuntan al MetricStore.
//
// Generar core:
//   ./tools/generate_core_demo.sh
//
// Probar en tuide:
//   ./build/tuide --core examples/cores/core_analyzer_demo.core ./build/core_analyzer_demo --core-analyzer
//
// Comandos útiles en la pestaña CoreAn:
//   obj (MetricStore*)0          → instancias por vtable
//   obj (ConsoleSink*)0
//   ref 0x<addr MetricStore>     → quién referencia el objeto oculto

#include <iostream>
#include <string>
#include <unistd.h>

namespace ca_demo {

// Objeto "oculto": solo en heap, sin símbolo en el stack al crashear.
class MetricStore {
 public:
  virtual ~MetricStore() = default;
  virtual const char* kind() const { return "MetricStore"; }

  int batch_id = 0xCAFE;
  double samples[8] = {};
  char secret_tag[24] = "HIDDEN_METRICS";
};

class IEventSink {
 public:
  virtual ~IEventSink() = default;
  virtual void on_event(int code) = 0;

  MetricStore* metrics = nullptr;
  int last_code = 0;
};

class ConsoleSink final : public IEventSink {
 public:
  void on_event(int code) override {
    last_code = code;
    if (metrics != nullptr) {
      std::cout << "ConsoleSink: batch=" << metrics->batch_id
                << " tag=" << metrics->secret_tag << '\n';
    }
    // Crash deliberado aquí — el analizador debe partir de este frame.
    volatile int* trap = nullptr;
    *trap = code;
  }
};

class FileSink final : public IEventSink {
 public:
  std::string path = "/var/log/events.log";

  void on_event(int code) override {
    last_code = code;
    std::cout << "FileSink(" << path << "): code=" << code << '\n';
  }
};

class EventRouter {
 public:
  IEventSink* primary = nullptr;
  IEventSink* secondary = nullptr;
  MetricStore* store = nullptr;

  void dispatch(int code) {
    if (primary != nullptr) {
      primary->metrics = store;
      primary->on_event(code);
    }
  }
};

}  // namespace ca_demo

int main() {
  using namespace ca_demo;

  auto* store = new MetricStore();
  store->batch_id = 4242;
  for (int i = 0; i < 8; ++i) {
    store->samples[i] = static_cast<double>(i) * 1.5;
  }

  auto* console = new ConsoleSink();
  auto* file = new FileSink();
  auto* router = new EventRouter();
  router->primary = console;
  router->secondary = file;
  router->store = store;

  std::cout << "PID " << getpid() << " — crasheando en ConsoleSink::on_event\n"
            << std::flush;

  router->dispatch(99);
  return 0;
}
