#!/usr/bin/env python3
import os
import socket
import sys
import select
from select import EPOLLIN, EPOLLPRI, EPOLLERR
import time
from collections import namedtuple
import argparse

TIMEOUT = 1.0 # seconds

VERSION_HEADER = bytearray('MangoHudControlVersion', 'utf-8')
DEVICE_NAME_HEADER = bytearray('DeviceName', 'utf-8')
MANGOHUD_VERSION_HEADER = bytearray('MangoHudVersion', 'utf-8')

DEFAULT_SERVER_ADDRESS = "\0mangohud"

class Connection:
    def __init__(self, path):
        # Create a Unix Domain socket and connect
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(path)
        except socket.error as msg:
            print(msg)
            sys.exit(1)

        self.sock = sock

        # initialize poll interface and register socket
        epoll = select.epoll()
        epoll.register(sock, EPOLLIN | EPOLLPRI | EPOLLERR)
        self.epoll = epoll

    def recv(self, timeout):
        '''
        timeout as float in seconds
        returns:
            - None on error or disconnection
            - bytes() (empty) on timeout
        '''

        events = self.epoll.poll(timeout)
        for ev in events:
            (fd, event) = ev
            if fd != self.sock.fileno():
                continue

            # check for socket error
            if event & EPOLLERR:
                return None

            # EPOLLIN or EPOLLPRI, just read the message
            msg = self.sock.recv(4096)

            # socket disconnected
            if len(msg) == 0:
                return None

            return msg

        return bytes()

    def send(self, msg):
        self.sock.send(msg)

class MsgParser:
    MSGBEGIN = bytes(':', 'utf-8')[0]
    MSGEND = bytes(';', 'utf-8')[0]
    MSGSEP = bytes('=', 'utf-8')[0]

    def __init__(self, conn):
        self.cmdpos = 0
        self.parampos = 0
        self.bufferpos = 0
        self.reading_cmd = False
        self.reading_param = False
        self.buffer = None
        self.cmd = bytearray(4096)
        self.param = bytearray(4096)

        self.conn = conn

    def readCmd(self, ncmds, timeout=TIMEOUT):
        '''
        returns:
            - None on error or disconnection
            - bytes() (empty) on timeout
        '''

        parsed = []

        remaining = timeout

        while remaining > 0 and ncmds > 0:
            now = time.monotonic()

            if self.buffer == None:
                self.buffer = self.conn.recv(remaining)
                self.bufferpos = 0

            # disconnected or error
            if self.buffer == None:
                return None

            for i in range(self.bufferpos, len(self.buffer)):
                c = self.buffer[i]
                self.bufferpos += 1
                if c == self.MSGBEGIN:
                    self.cmdpos = 0
                    self.parampos = 0
                    self.reading_cmd = True
                    self.reading_param = False
                elif c == self.MSGEND:
                    if not self.reading_cmd:
                        continue
                    self.reading_cmd = False
                    self.reading_param = False

                    cmd = self.cmd[0:self.cmdpos]
                    param = self.param[0:self.parampos]
                    self.reading_cmd = False
                    self.reading_param = False

                    parsed.append((cmd, param))
                    ncmds -= 1
                    if ncmds == 0:
                        break
                elif c == self.MSGSEP:
                    if self.reading_cmd:
                        self.reading_param = True
                else:
                    if self.reading_param:
                        self.param[self.parampos] = c
                        self.parampos += 1
                    elif self.reading_cmd:
                        self.cmd[self.cmdpos] = c
                        self.cmdpos += 1

            # if we read the entire buffer and didn't finish the command,
            # throw it away
            self.buffer = None

            # check if we have time for another iteration
            elapsed = time.monotonic() - now
            remaining = max(0, remaining - elapsed)

        # timeout
        return parsed

def control(args):
    if args.socket:
        address = '\0' + args.socket
    else:
        address = DEFAULT_SERVER_ADDRESS

    conn = Connection(address)
    msgparser = MsgParser(conn)

    version = None
    name = None
    mangohud_version = None

    msgs = msgparser.readCmd(3)

    for m in msgs:
        cmd, param = m
        if cmd == VERSION_HEADER:
            version = int(param)
        elif cmd == DEVICE_NAME_HEADER:
            name = param.decode('utf-8')
        elif cmd == MANGOHUD_VERSION_HEADER:
            mangohud_version = param.decode('utf-8')

    if args.info:
        info = "Protocol Version: {}\n"
        info += "Device Name: {}\n"
        info += "MangoHud Version: {}"
        print(info.format(version, name, mangohud_version))


    if args.cmd == 'toggle-logging':
        conn.send(bytearray(':logging;', 'utf-8'))
    elif args.cmd == 'start-logging':
        conn.send(bytearray(':logging=1;', 'utf-8'))

    elif args.cmd == 'stop-logging':
        conn.send(bytearray(':logging=0;', 'utf-8'))
        now = time.monotonic()
        while True:
            msg = str(conn.recv(3))
            if "LoggingFinished" in msg:
                print("Logging has stopped")
                exit(0)
            elapsed = time.monotonic() - now
            if elapsed > 3:
                print("Stop logging timed out")
                exit(1)

    elif args.cmd == 'toggle-hud':
        conn.send(bytearray(':hud;', 'utf-8'))
    elif args.cmd == 'toggle-fcat':
        conn.send(bytearray(':fcat;', 'utf-8'))
    elif args.cmd == 'set-fps-limit':
        msg = ':set_fps_limit={};'.format(args.fps)
        conn.send(bytearray(msg, 'utf-8'))

def main():
    parser = argparse.ArgumentParser(description='MangoHud control client')
    parser.add_argument('--info', action='store_true', help='Print info from socket')
    parser.add_argument('--socket', '-s', type=str, help='Path to socket')

    commands = parser.add_subparsers(help='commands to run', dest='cmd')
    commands.add_parser('toggle-hud')
    commands.add_parser('toggle-logging')
    commands.add_parser('start-logging')
    commands.add_parser('stop-logging')
    commands.add_parser('toggle-fcat')
    set_fps_limit_parser = commands.add_parser('set-fps-limit')
    set_fps_limit_parser.add_argument('fps', type=float, help='FPS limit value (0 for unlimited)')

    args = parser.parse_args()

    control(args)

if __name__ == '__main__':
    main()
