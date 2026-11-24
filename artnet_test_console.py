# Art-Net test console: sends DMX to the game so you can try Stage Lights without lighting software.
#   python tools/artnet_test_console.py [seconds]
# Turn on Settings > Video > Art-Net input (This computer), auto-patch from universe 0 on the
# Lighting Desk, press Take DMX control. 4 moving heads (1, 13, 25, 37) sweep red with a star
# gobo; 2 washes (49, 57) light green.
# A stand-in lighting console: sends Art-Net (ArtDmx) to the game, universe 0, ~30 times a second
import socket, struct, time, math, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
seq = 0
t0 = time.time()
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 120
while time.time() - t0 < dur:
    t = time.time() - t0
    d = bytearray(512)
    # 4 moving heads (12 ch) at 1, 13, 25, 37: sweeping pan, tilt, full, red, a star gobo spinning
    for i in range(4):
        a = i * 12
        pan = int((0.5 + 0.18 * math.sin(t * 1.5 + i)) * 65535)
        tilt = int((0.22 + 0.05 * math.sin(t * 2 + i)) * 65535)      # (about 50 degrees down)
        d[a+0], d[a+1] = pan >> 8, pan & 255
        d[a+2], d[a+3] = tilt >> 8, tilt & 255
        d[a+4] = 255; d[a+5] = 0
        d[a+6], d[a+7], d[a+8] = 255, 20, 10
        d[a+9] = 80; d[a+10] = 15; d[a+11] = 170
    # 2 washes (8 ch) at 49, 57: green, aimed up at the back wall
    for i in range(2):
        a = 48 + i * 8
        d[a+0] = 128; d[a+1] = int(0.75 * 255); d[a+2] = 255; d[a+3] = 0
        d[a+4], d[a+5], d[a+6] = 10, 255, 40; d[a+7] = 200
    seq = seq % 255 + 1
    pkt = b"Art-Net\x00" + struct.pack("<H", 0x5000) + bytes([0, 14, seq, 0, 0, 0]) + struct.pack(">H", 512) + bytes(d)
    s.sendto(pkt, ("127.0.0.1", 6454))
    time.sleep(1 / 30)
