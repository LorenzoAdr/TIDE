// Ejemplo mínimo para outline / LSP zls en tgdb.
// Compilar: zig build-exe -ODebug hello.zig

const std = @import("std");

fn greet(name: []const u8) void {
    std.debug.print("hello, {s}\n", .{name});
}

pub fn main() !void {
    greet("tide");
}