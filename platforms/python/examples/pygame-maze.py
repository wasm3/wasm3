#!/usr/bin/env python3

import os, time, random, math
import pygame
import wasm3

print("WebAssembly demo file provided by Ben Smith (binji)")
print("Sources: https://github.com/binji/raw-wasm")

scriptpath = os.path.dirname(os.path.realpath(__file__))
wasm_fn = os.path.join(scriptpath, "./wasm/maze.wasm")

def env_t(start):
    pass

# Prepare Wasm3 engine

env = wasm3.Environment()
rt = env.new_runtime(1024)
with open(wasm_fn, "rb") as f:
    mod = env.parse_module(f.read())
    rt.load(mod)
    mod.link_function("Math", "sin", "f(f)", math.sin)
    mod.link_function("Math", "random", "f()", random.random)
    mod.link_function("env", "t", "v(i)", env_t)

wasm_run = rt.find_function("run")
mem = rt.get_memory(0)

# Map memory region to an RGBA image

img_base = 0x3000
img_size = (320, 240)
(img_w, img_h) = img_size
region = mem[img_base : img_base + (img_w * img_h * 4)]
img = pygame.image.frombuffer(region, img_size, "RGBA")

# Prepare PyGame

scr_size = (img_w*2, img_h*2)
pygame.init()
surface = pygame.display.set_mode(scr_size)
pygame.display.set_caption("Wasm3 Maze")
white = (255, 255, 255)

k_up    = False
k_down  = False
k_left  = False
k_right = False

clock = pygame.time.Clock()

while True:
    # Process input
    for event in pygame.event.get():
        if (event.type == pygame.QUIT or
            (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE)):
            pygame.quit()
            quit()
        elif event.type == pygame.KEYDOWN or event.type == pygame.KEYUP:
            is_pressed = (event.type == pygame.KEYDOWN)
            if event.key == pygame.K_UP:
                k_up = is_pressed
            elif event.key == pygame.K_DOWN:
                k_down = is_pressed
            elif event.key == pygame.K_LEFT:
                k_left = is_pressed
            elif event.key == pygame.K_RIGHT:
                k_right = is_pressed

    mem[0] = k_left
    mem[1] = k_right
    mem[2] = k_up
    mem[3] = k_down

    # Render next frame
    wasm_run()

    # Image output
    img_scaled = pygame.transform.scale(img, scr_size)
    surface.fill(white)
    surface.blit(img_scaled, (0, 0))
    pygame.display.flip()

    # Stabilize FPS
    clock.tick(60)
