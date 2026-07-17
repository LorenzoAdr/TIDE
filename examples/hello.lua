#!/usr/bin/env lua
-- Ejemplo mínimo para outline / LSP lua-language-server en tgdb.
-- Ejecutar: lua hello.lua

local function greet(name)
  print("hello, " .. (name or "world"))
end

greet("tide")
