//! Ejemplo mínimo para outline / LSP rust-analyzer en tgdb.
//! Requiere el `Cargo.toml` de este directorio (`examples/`).
//! Compilar: `cargo build --manifest-path examples/Cargo.toml --bin hello`
//! O: `rustc -g hello.rs`

fn greet(name: &str) {
    println!("hello, {name}");
}

fn main() {
    greet("tide");
   
    
}