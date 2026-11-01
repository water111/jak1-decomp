# Jak 1 Decompilation

This project is to build a matching decompilation of Jak and Daxter. Currently, only Linux is supported, but Windows support is planned.

## Host Tools

The host tools are written in Rust.

## PS2 Toolchain

While over 99% of the game's code is in GOAL, we still have a small amount of C/C++ code to build for the PS2.

This toolchain itself is built from source in an Alpine Linux docker container. In the future there may be pre-built versions of this.

## Asset extraction

Game art assets aren't included and must be extracted from your game first.

## Game Build System

The full game is built as a 

