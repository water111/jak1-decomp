# Jak 1 Decompilation

This project is to build a matching decompilation of Jak and Daxter. Currently, only Linux is supported, but Windows support is planned.

## Host Tools

The host tools are written in Rust.

## PS2 Toolchain

While over 99% of the game's code is in GOAL, we still have a small amount of C/C++ code to build for the PS2.

This toolchain itself is built from source in an Alpine Linux docker container. In the future there may be pre-built versions of this.

### IOP Module Building

The I/O Processor (IOP) runs .IRX modules. The build process for this is somewhat complicated:

- Sony libraries have a `.tbl` file (recreated) defining their exports.
- We recreate `ioplibgen` to convert `.tbl` to `.ilb`, which defines exports and their ID.
- We recreate `ioplibld` to look at IOP object files and `.ilb` and produce stubs
- Those stubs are linked into the final IOP object
- We recreate `iopfixup` to build the final `.IRX`.

## Asset extraction

Game art assets aren't included and must be extracted from your game first.

## Game Build System

The full game is built as a 

