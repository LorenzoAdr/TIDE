							#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/prctl.h>
#include <unistd.h>

namespace {

// Linux yama (ptrace_scope=1): solo procesos hijo son depurables salvo que el
// objetivo permita trazadores externos con PR_SET_PTRACER.
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

void allow_external_debugger() {
  if (prctl(PR_SET_PTRACER, -1) != 0) {
    std::cerr << "aviso: no se pudo permitir attach externo (prctl): "
              << std::strerror(errno) << "\n"
              << "  prueba: sudosss sysctl kernel.yama.ptrace_scope=0\n"
              << "  o usa: ./tools/launch.sh ./build/hello\n";
  }
}

int accumulate(int n) {
  int total = 0;
  for (int i = 1; i <= n; ++i) {
    total += i;
  }
  return total;
}

}  // namespace

int main() {
  allow_external_debugger();

  const pid_t pid = getpid();
  std::cout << "hello PID " << pid << "\n"
            << "Adjunta con: ./tools/launch.sh --attach " << pid
            << " ./build/hello\n"
            << std::flush;

  int counter = 0;
  int x = 10;
  int y = 20;

  while (true) {
    ++counter;
    x = counter * 3;
    y = x + 7;
    const int sum = x + y;
    const int acc = accumulate(counter % 20);

    std::cout << "[" << counter << "] x=" << x << " y=" << y
              << " sum=" << sum << " acc=" << acc << std::endl;
    std::cout << "prueba";
    sleep(1);
  }

  return 0;
}