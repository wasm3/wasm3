#!/usr/bin/env python3

import wasm3
import os, time, random, math
import pygame

print("WebAssembly demo file provided by Ben Smith (binji)")
print("Sources: https://github.com/binji/raw-wasm")

scriptpath = os.path.dirname(os.path.realpath(__file__))
wasm_fn = os.path.join(scriptpath, "./wasm/metaball.wasm")

# Prepare Wasm3 engine

env = wasm3.Environment()
rt = env.new_runtime(1024)
with open(wasm_fn, "rb") as f:
    mod = env.parse_module(f.read())
    rt.load(mod)
    mod.link_function("", "rand", "f()", random.random)
    wasm_blobs = rt.find_function("blobs")
    wasm_blobs(5)

wasm_run = rt.find_function("run")
mem = rt.get_memory(0)

# Map memory region to an RGBA image

img_base = 1024
img_size = (320, 200)
(img_w, img_h) = img_size
region = mem[img_base : img_base + (img_w * img_h * 4)]
img = pygame.image.frombuffer(region, img_size, "RGBA")

# Prepare PyGame

scr_size = (img_w*2, img_h*2)
pygame.init()
surface = pygame.display.set_mode(scr_size)
pygame.display.set_caption("Wasm3 Metaball")
background = (0xd4, 0x19, 0x5d)

clock = pygame.time.Clock()

while True:
    # Process input
    for event in pygame.event.get():
        if (event.type == pygame.QUIT or
            (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE)):
            pygame.quit()
            quit()

    # Render next frame
    wasm_run()

    # Image output
    img_scaled = pygame.transform.scale(img, scr_size)
    surface.fill(background)
    surface.blit(img_scaled, (0, 0))
    pygame.display.flip()

    # Stabilize FPS
    clock.tick(30)
