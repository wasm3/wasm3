#!/usr/bin/env python3

import os, struct, time
import multiprocessing as mp
import wasm3
import numpy

os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "true"

def player(q):
    import pygame
    pygame.mixer.pre_init(frequency=22050, size=-16, channels=2)
    pygame.init()

    channel = pygame.mixer.Channel(0)
    while True:
        buff = q.get()
        if not len(buff):
            break
        chunk = pygame.mixer.Sound(buffer=buff)

        indicator = '|' if channel.get_queue() else '.'
        print(indicator, end='', flush=True)

        while channel.get_queue() is not None:
            time.sleep(0.01)

        channel.queue(chunk)

    pygame.quit()


if __name__ == '__main__':

    print("Hondarribia - Intro song WebAssembly Summit 2020 by Peter Salomonsen")
    print("Source:      https://petersalomonsen.com/webassemblymusic/livecodev2/?gist=5b795090ead4f192e7f5ee5dcdd17392")
    print("Synthesized: https://soundcloud.com/psalomo/hondarribia")

    q = mp.Queue()
    p = mp.Process(target=player, args=(q,))
    p.start()

    scriptpath = os.path.dirname(os.path.realpath(__file__))
    wasm_fn = os.path.join(scriptpath, "./wasm/hondarribia_22050.wasm")

    # Prepare Wasm3 engine

    env = wasm3.Environment()
    rt = env.new_runtime(1024)
    with open(wasm_fn, "rb") as f:
        mod = env.parse_module(f.read())
        rt.load(mod)

    buff = b''
    buff_sz = 512
    print("Pre-buffering...")

    def fd_write(fd, wasi_iovs, iows_len, nwritten):
        global buff, buff_sz
        mem = rt.get_memory(0)

        # get data
        (off, size) = struct.unpack("<II", mem[wasi_iovs:wasi_iovs+8])
        data = mem[off:off+size]

        # decode
        arr = numpy.frombuffer(data, dtype=numpy.float32) 
        data = (arr * 32768).astype(numpy.int16).tobytes()

        # buffer
        buff += data
        if len(buff) > buff_sz*1024:
            q.put(buff)
            buff = b''
            buff_sz = 128

        return size

    mod.link_function("wasi_snapshot_preview1", "fd_write", "i(i*i*)", fd_write)

    wasm_start = rt.find_function("_start")
    wasm_start()

    q.put(b'')

    p.join()

    print()
    print("Finished")

