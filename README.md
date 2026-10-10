# Framebuffer Mouse

A minimal useless C program that draws a mouse cursor directly into `/dev/fb0` and reads mouse movement from `/dev/input/mice` — no X11, no Wayland, no graphical stack.

## What it does

- Opens `/dev/fb0`, queries screen geometry via `FBIOGET_VSCREENINFO` / `FBIOGET_FSCREENINFO`.
- Maps the framebuffer into memory with `mmap`.
- Reads 3-byte packets from `/dev/input/mice` (blocking read, one redraw per event).
- Draws an 8x12 arrow cursor, saving the pixels underneath and restoring them before each move to avoid trails.

## Requirements

- Linux with a legacy framebuffer (`/dev/fb0` must exist).
- A real TTY (Ctrl+Alt+F3). It will not work under X11 or Wayland — those own the GPU and input devices.
- Root, or membership in the `input` group to read `/dev/input/mice`.
- `gpm` (General Purpose Mouse) must be stopped, otherwise two cursors will fight.

## Build and run

```bash
gcc fb_mouse.c -o fb_mouse
sudo ./fb_mouse
