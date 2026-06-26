# tgdb

Depurador TUI estilo Visual Studio para C++, construido con [FTXUI](https://github.com/ArthurSonzogni/FTXUI) y conectado a GDB mediante el [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (`gdb -i=dap`).

## Requisitos

- Linux
- CMake 3.20+
- Compilador C++17
- GDB 14+ con soporte DAP (Python habilitado)

```bash
gdb --version
gdb -i=dap -ex quit
```

## Compilación

```bash
cmake -S . -B build
cmake --build build
```

Esto genera:

- `build/tgdb` — depurador TUI
- `build/hello` — ejemplo con símbolos de depuración

## Uso

```bash
./tools/compile.sh
./tools/launch.sh

# Attach a proceso local (necesitas permisos, p. ej. ptrace):
./tools/launch.sh --attach <PID> ./build/hello

# Attach a gdbserver remoto:
./tools/launch.sh --target localhost:1234 ./build/hello
```

`--cwd` es el directorio del workspace. `<programa>` siempre es el binario con símbolos (`-g`), también en attach.

```bash
./build/tgdb ./build/hello
./build/tgdb --attach 12345 ./build/hello
./build/tgdb --target localhost:1234 ./build/hello
```

## Layout

- **Izquierda**: explorador de archivos del workspace (`.c`, `.cpp`, `.h`, ...)
- **Centro**: código con línea actual, breakpoints y gutter
- **Derecha**: watches, variables locales y call stack
- **Abajo**: consola GDB (`continue`, `-exec info locals`, `watch expr`, ...)

## Atajos

| Tecla | Acción |
|-------|--------|
| F5 | Continue |
| F10 | Step over |
| F11 | Step into |
| o / Shift+F11 | Step out |
| Espacio | Toggle breakpoint en línea actual |
| Click gutter | Toggle breakpoint |
| q | Salir |

## Arquitectura

- **Hilo UI**: FTXUI (`ScreenInteractive::Loop`)
- **Hilo DAP**: cliente `cppdap` ↔ `gdb --interpreter=dap`
- Comunicación por colas thread-safe (`UiCommand` / `DebugEvent`)
- `DebugModel` compartido solo desde el hilo UI

## Notas

- El árbol de archivos se obtiene del filesystem local; DAP no lista el workspace.
- La consola usa `evaluate` con contexto `repl` para comandos GDB.
- Para watches: `watch mi_variable` en la consola.
