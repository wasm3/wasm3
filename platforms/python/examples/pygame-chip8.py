#!/usr/bin/env python3

import wasm3
import os, time, random, math, base64
import pygame

print("WebAssembly demo file provided by Ben Smith (binji)")
print("Sources: https://github.com/binji/raw-wasm")

scriptpath = os.path.dirname(os.path.realpath(__file__))
wasm_fn = os.path.join(scriptpath, "./wasm/chip8.wasm")

# Prepare Wasm3 engine

env = wasm3.Environment()
rt = env.new_runtime(1024)
with open(wasm_fn, "rb") as f:
    mod = env.parse_module(f.read())
    rt.load(mod)
    mod.link_function("Math", "random", "f()", random.random)

wasm_run = rt.find_function("run")
mem = rt.get_memory(0)

# Load CHIP-8 ROM

ROM = base64.b64decode("""
    YwfB/6Js8R7wZUAAEgKEAMUfokz1HvBlyANIABImhkCGAkYAEgKjbIBg8FWEY6Js8R6AQPBVo2yC
    EIEygR6BHoEegiaCJoIm0SESAgMGDBgwYMCBBw4cOHDgwYMPHjx48OHDhx8+fPjx48eP////////
    ////x/////////+D/////////wI/////////BB/4Z88TP/yED/mnjnN/+GgP+aeWcP/4GBP4ZzZw
    //AEIfmnAnN/4AJA+CE6cz/AAoH4YTsTP8BDAn//////gP4EP/////+A/wg5ydDD/8A/kHnJ05//
    wA/g+cnTn//gB+F5zLDH//ADEnnMs+P/+AMM+E5wh//8AwD4TnCH//wBAP///////gEA////////
    AQH5zmEIR/8AA/nMYQnD/4AD+Eyzmcv/gAf4CbOYQ/8Hh/koE5nH/wOH+SnTmEP/AQf56dOYSf8B
    B////////wEH////////AQf//////wA=
""")

mem[0x200:0x200+len(ROM)] = ROM

# Map memory region to an RGBA image

img_base = 0x1000
img_size = (64, 32)
(img_w, img_h) = img_size
region = mem[img_base : img_base + (img_w * img_h * 4)]
img = pygame.image.frombuffer(region, img_size, "RGBA")

# Prepare PyGame

scr_size = (img_w*8, img_h*8)
pygame.init()
surface = pygame.display.set_mode(scr_size)
pygame.display.set_caption("Wasm3 CHIP-8")
white = (255, 255, 255)

clock = pygame.time.Clock()

while True:
    # Process input
    for event in pygame.event.get():
        if (event.type == pygame.QUIT or
            (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE)):
            pygame.quit()
            quit()

    # TODO: input support
    #mem[10] = 0

    # Render next frame
    wasm_run(500)

    # Image output
    img_scaled = pygame.transform.scale(img, scr_size)
    surface.fill(white)
    surface.blit(img_scaled, (0, 0))
    pygame.display.flip()

    # Stabilize FPS
    clock.tick(60)
