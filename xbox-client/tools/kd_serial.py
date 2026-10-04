#!/usr/bin/env python3
"""Small interactive serial KD diagnostic client (not a WinDbg replacement).

Wire constants/layouts: ReactOS sdk/include/reactos/windbgkd.h.
Context/memory reads, continue, break-in and explicit reboot are exposed.
Explicit resume-break may advance EIP past a verified INT3 instruction; no
arbitrary memory/register writes or automatic exception continuation.
"""
import argparse
import os
import select
import struct
import sys
import termios


class KD:
    def __init__(self, fd):
        self.fd = fd
        self.buffer = bytearray()
        self.txid = 0x80800000
        self.pending = None
        self.last_rx = None
        self.stopped = False
        self.exception = None
        self.resume_stage = None
        self.context = None
        self.resends = 0

    def write(self, data):
        while data:
            try:
                n = os.write(self.fd, data)
                data = data[n:]
            except BlockingIOError:
                if not select.select([], [self.fd], [], 2)[1]:
                    raise TimeoutError('Serial write timed out')

    def control(self, kind, ident=0):
        self.write(struct.pack('<IHHII', 0x69696969, kind, 0, ident, 0))

    def request(self, api, body=b'', extra=b''):
        if self.pending:
            print('Previous request still awaiting ACK; not sending another.')
            return
        if not self.stopped:
            print('Target is not stopped; use break first.')
            return
        payload = struct.pack('<IHHI', api, 6, 0, 0) + bytes(4) + body
        payload = payload.ljust(56, b'\0') + extra
        wire = struct.pack('<IHHII', 0x30303030, 2, len(payload), self.txid,
                           sum(payload) & 0xffffffff) + payload + b'\xaa'
        self.pending = wire
        self.resends = 0
        self.write(wire)
        print('TX api=%04x id=%08x' % (api, self.txid))
        if api in (0x3136, 0x313b):
            self.stopped = False

    def feed(self, data):
        self.buffer.extend(data)
        while len(self.buffer) >= 16:
            leader, kind, size, ident, checksum = struct.unpack_from('<IHHII', self.buffer)
            if leader not in (0x30303030, 0x69696969) or size > 4000:
                del self.buffer[0]
                continue
            count = 16 if leader == 0x69696969 else 17 + size
            if len(self.buffer) < count:
                break
            payload = bytes(self.buffer[16:16+size])
            valid = leader == 0x69696969 or (self.buffer[count-1] == 0xaa and
                    sum(payload) & 0xffffffff == checksum)
            del self.buffer[:count]
            if not valid:
                print('Bad checksum/trailer; requesting resend')
                self.control(5)
                continue
            if leader == 0x69696969:
                print('CONTROL type=%d id=%08x' % (kind, ident))
                if kind == 4 and ident == self.txid:
                    self.txid ^= 1
                    self.pending = None
                elif kind == 5 and self.pending:
                    if self.resends < 3:
                        self.resends += 1
                        self.write(self.pending)
                    else:
                        print('Repeated resend requests; reconnect the KD client')
                elif kind == 6:
                    self.control(6)
                    self.txid = 0x80800000
                    self.last_rx = None
                    self.pending = None
                continue
            self.control(4, ident & ~0x800)
            if ident & 0x800:
                self.txid = 0x80800000
                self.pending = None
                self.last_rx = None
            key = (ident, kind, payload)
            if key == self.last_rx:
                continue
            self.last_rx = key
            print('RX type=%d id=%08x bytes=%d' % (kind, ident, size))
            if kind == 7 and size >= 32:
                self.stopped = True
                state, = struct.unpack_from('<I', payload)
                thread, pc = struct.unpack_from('<QQ', payload, 16)
                print('STATE=%04x thread=%016x PC=%016x' % (state, thread, pc))
                if state == 0x3030 and size >= 36:
                    self.exception = struct.unpack_from('<I', payload, 32)[0]
                    print('EXCEPTION=%08x' % self.exception)
                else:
                    self.exception = None
                print('STATE prefix:', payload[:80].hex())
                if state == 0x3031:
                    # Module load/unload notifications are not exceptions.
                    self.request(0x3136, struct.pack('<I', 0x10002))
            elif kind == 3 and size >= 16:
                print('DEBUG:', payload[16:].decode('utf-8', 'replace'))
            elif kind == 2 and size >= 56:
                api, level, cpu, status = struct.unpack_from('<IHHI', payload)
                print('REPLY api=%04x status=%08x union=%s' % (api, status, payload[16:56].hex()))
                extra = payload[56:]
                if self.resume_stage and status:
                    print('Resume aborted: request failed')
                    self.resume_stage = None
                if api == 0x3132 and status == 0 and len(extra) >= 204:
                    self.context = bytearray(extra)
                    for label, offset in [('EDI',156),('ESI',160),('EBX',164),('EDX',168),
                                          ('ECX',172),('EAX',176),('EBP',180),('EIP',184),('ESP',196)]:
                        print('%s=%08x' % (label, struct.unpack_from('<I', extra, offset)[0]))
                    if self.resume_stage == 'context':
                        pc = struct.unpack_from('<I', extra, 184)[0]
                        address = pc | (0xffffffff00000000 if pc & 0x80000000 else 0)
                        self.resume_stage = 'opcode'
                        self.request(0x3130, struct.pack('<QII', address, 1, 0))
                elif api == 0x3130 and self.resume_stage == 'opcode' and status == 0:
                    self.resume_stage = None
                    if extra == b'\xcc':
                        pc = struct.unpack_from('<I', self.context, 184)[0]
                        struct.pack_into('<I', self.context, 184, pc + 1)
                        self.resume_stage = 'set-context'
                        self.request(0x3133, self.context[:4], self.context)
                    else:
                        print('Resume aborted: EIP does not point to INT3')
                elif api == 0x3133 and self.resume_stage == 'set-context' and status == 0:
                    self.resume_stage = None
                    self.request(0x3136, struct.pack('<I', 0x10002))
                for offset in range(0, len(extra), 16):
                    row = extra[offset:offset+16]
                    print('%04x %s' % (offset, row.hex(' ')))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('port')
    args = ap.parse_args()
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    old = termios.tcgetattr(fd)
    mode = termios.tcgetattr(fd)
    mode[:4] = [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0]
    mode[4:6] = [termios.B115200, termios.B115200]
    mode[6][termios.VMIN] = mode[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, mode)
    kd = KD(fd)
    # Synchronize packet IDs when attaching to a kernel already stopped by a
    # previous client. This resets the KD transport, not the console.
    kd.control(6)
    print('KD ready: 115200/8N1. Commands: break, version, context, read HEX SIZE, continue, resume-break, reboot, quit', flush=True)
    try:
        while True:
            ready, _, _ = select.select([fd, sys.stdin], [], [], 1)
            if fd in ready:
                kd.feed(os.read(fd, 8192))
            if sys.stdin in ready:
                line = sys.stdin.readline()
                if not line:
                    break
                cmd = line.split()
                if not cmd:
                    continue
                if cmd[0] == 'quit':
                    break
                if cmd[0] == 'break':
                    kd.write(b'b')
                elif cmd[0] == 'version':
                    kd.request(0x3146)
                elif cmd[0] == 'context':
                    kd.request(0x3132)
                elif cmd[0] == 'read' and len(cmd) == 3:
                    try:
                        address = int(cmd[1], 16)
                        size = int(cmd[2], 0)
                        if not 0 <= address <= 0xffffffffffffffff or not 1 <= size <= 2048:
                            raise ValueError()
                    except ValueError:
                        print('Use a hexadecimal address and a byte count from 1 to 2048')
                        continue
                    if address & 0x80000000 and address <= 0xffffffff:
                        address |= 0xffffffff00000000
                    kd.request(0x3130, struct.pack('<QII', address, size, 0))
                elif cmd[0] == 'continue':
                    kd.request(0x3136, struct.pack('<I', 0x10002))
                elif cmd[0] == 'resume-break':
                    if kd.exception != 0x80000003:
                        print('Only valid for a confirmed breakpoint exception')
                    else:
                        kd.resume_stage = 'context'
                        kd.request(0x3132)
                elif cmd[0] == 'reboot':
                    kd.request(0x313b)
            sys.stdout.flush()
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, old)
        os.close(fd)


if __name__ == '__main__':
    main()
