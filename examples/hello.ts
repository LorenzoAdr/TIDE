// Ejemplo mínimo para outline / LSP typescript-ls en tgdb.
// (sin DAP TypeScript aún)
// Compilar/ejecutar: npx ts-node hello.ts  o  tsc hello.ts && node hello.js

function greet(name: string): void {
  console.log(`hello, ${name}`);
}

greet("tide");
