//! Ejemplo mínimo para outline / LSP rust-analyzer en tgdb.
//! Compilar: rustc -g hello.rs
//! O con cargo: cargo new hello && reemplazar src/main.rs

fn greet(name: &str) {
    println!("hello, {name}");
}

fn main() {
    greet("tide");
}
