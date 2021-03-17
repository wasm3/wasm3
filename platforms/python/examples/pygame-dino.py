#!/usr/bin/env python3

import wasm3
import os, time, random
import pygame

print("WebAssembly demo file provided by Ben Smith (binji)")
print("Sources: https://github.com/binji/raw-wasm")

scriptpath = os.path.dirname(os.path.realpath(__file__))
wasm_fn = os.path.join(scriptpath, "./wasm/dino.wasm")

# Prepare Wasm3 engine

env = wasm3.Environment()
rt = env.new_runtime(1024)
with open(wasm_fn, "rb") as f:
    mod = env.parse_module(f.read())
    rt.load(mod)
    mod.link_function("Math", "random", "f()", lambda: random.random())

wasm_run = rt.find_function("run")
mem = rt.get_memory(0)

# Map memory region to an RGBA image

img_base = 0x5000
img_size = (300, 75)
(img_w, img_h) = img_size
region = mem[img_base : img_base + (img_w * img_h * 4)]
img = pygame.image.frombuffer(region, img_size, "RGBA")

# Prepare PyGame

scr_size = (img_w*4, img_h*4)
pygame.init()
surface = pygame.display.set_mode(scr_size)
pygame.display.set_caption("Wasm3 Dino")
white = (255, 255, 255)

k_jump = False
k_duck = False

clock = pygame.time.Clock()

while True:
    # Process input
    for event in pygame.event.get():
        if (event.type == pygame.QUIT or
            (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE)):
            pygame.quit()
            quit()
        elif event.type == pygame.KEYDOWN or event.type == pygame.KEYUP:
            if event.key == pygame.K_UP:
                k_jump = (event.type == pygame.KEYDOWN)
            elif event.key == pygame.K_DOWN:
                k_duck = (event.type == pygame.KEYDOWN)

    mem[0] = 0
    if k_jump:
        mem[0] |= 0x1       # Jump flag
    if k_duck:
        mem[0] |= 0x2       # Duck flag

    # Render next frame
    wasm_run()

    # Image output
    img_scaled = pygame.transform.scale(img, scr_size)
    surface.fill(white)
    surface.blit(img_scaled, (0, 0))
    pygame.display.flip()

    # Stabilize FPS
    clock.tick(60)
