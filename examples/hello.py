#!/usr/bin/env python3
"""Equivalente de examples/hello.cpp: bucle UDP con paquetes heartbeat/sensor.

Útil para probar LSP (basedpyright) y DAP (debugpy) en tgdb.
Launch con packet monitor activo para capturar tráfico (solo aplica a binarios
nativos vía LD_PRELOAD; en Python el tráfico UDP sigue siendo visible en la red).
"""

from __future__ import annotations

import os
import socket
import struct
import sys
import time

UDP_PORT = 5555

# Empaquetado little-endian equivalente a los structs packed de hello.cpp.
# HeartbeatPacket: uint8 msg_type, uint8 reserved, uint16 sequence
HEARTBEAT_FMT = "<BBH"
# SensorPacket: uint8 msg_type, uint8 device_id, uint16 reserved, float temperature, uint16 status
SENSOR_FMT = "<BBHfH"


def allow_external_debugger() -> None:
    # En C++ se usa prctl(PR_SET_PTRACER). Con debugpy no hace falta; se deja
    # el hook por si se adjunta un depurador nativo al intérprete.
    try:
        import ctypes

        PR_SET_PTRACER = 0x59616D61
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        if libc.prctl(PR_SET_PTRACER, -1, 0, 0, 0) != 0:
            err = ctypes.get_errno()
            print(
                f"aviso: no se pudo permitir attach externo (prctl): {os.strerror(err)}",
                file=sys.stderr,
            )
    except OSError as exc:
        print(f"aviso: prctl no disponible: {exc}", file=sys.stderr)


def create_udp_socket() -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", UDP_PORT))
    sock.setblocking(False)
    return sock


def send_packet(sock: socket.socket, data: bytes) -> bool:
    sent = sock.sendto(data, ("127.0.0.1", UDP_PORT))
    return sent == len(data)


def try_receive(sock: socket.socket) -> None:
    try:
        data, _addr = sock.recvfrom(256)
    except BlockingIOError:
        return
    if data:
        print(f"udp recv {len(data)} bytes")


def pack_heartbeat(sequence: int) -> bytes:
    return struct.pack(HEARTBEAT_FMT, 0x01, 0, sequence & 0xFFFF)


def pack_sensor(device_id: int, temperature: float, status: int) -> bytes:
    return struct.pack(
        SENSOR_FMT,
        0x02,
        device_id & 0xFF,
        0,
        float(temperature),
        status & 0xFFFF,
    )


def main() -> int:
    allow_external_debugger()

    pid = os.getpid()
    print(
        f"hello PID {pid}\n"
        f"UDP demo en 127.0.0.1:{UDP_PORT}\n"
        f"Launch con packet monitor activo para capturar trafico.",
        flush=True,
    )

    try:
        udp = create_udp_socket()
    except OSError as exc:
        print(f"no se pudo abrir socket UDP: {exc}", file=sys.stderr)
        return 1

    sequence = 0
    device_id = 1
    counter = 0
    try:
        while True:
            counter += 1
            sequence = (sequence + 1) & 0xFFFF
            if counter % 3 == 0:
                send_packet(udp, pack_heartbeat(sequence))
                print(f"[{counter}] sent heartbeat seq={sequence}", flush=True)
            else:
                temperature = 20.0 + float(counter % 10)
                status = counter & 0x03
                send_packet(udp, pack_sensor(device_id, temperature, status))
                print(
                    f"[{counter}] sent sensor temp={temperature} device={device_id}",
                    flush=True,
                )

            try_receive(udp)
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nbye", flush=True)
    finally:
        udp.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())