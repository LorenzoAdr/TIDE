#pragma once

namespace tuide::toolpacks {

// argv[0] is the subcommand name ("toolpacks") when dispatched from main.
// Full: tuide toolpacks <cmd> ...
int run_cli(int argc, char** argv);

}  // namespace tuide::toolpacks
