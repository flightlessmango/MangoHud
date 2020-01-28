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

VERSION_HEADER = bytearray('MesaOverlayControlVersion', 'utf-8')
DEVICE_NAME_HEADER = bytearray('DeviceName', 'utf-8')
MESA_VERSION_HEADER = bytearray('MesaVersion', 'utf-8')

DEFAULT_SERVER_ADDRESS = "\0mesa_overlay"

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
    mesa_version = None

    msgs = msgparser.readCmd(3)

    for m in msgs:
        cmd, param = m
        if cmd == VERSION_HEADER:
            version = int(param)
        elif cmd == DEVICE_NAME_HEADER:
            name = param.decode('utf-8')
        elif cmd == MESA_VERSION_HEADER:
            mesa_version = param.decode('utf-8')

    if version != 1 or name == None or mesa_version == None:
        print('ERROR: invalid protocol')
        sys.exit(1)


    if args.info:
        info = "Protocol Version: {}\n"
        info += "Device Name: {}\n"
        info += "Mesa Version: {}"
        print(info.format(version, name, mesa_version))

    if args.cmd == 'start-capture':
        conn.send(bytearray(':capture=1;', 'utf-8'))
    elif args.cmd == 'stop-capture':
        conn.send(bytearray(':capture=0;', 'utf-8'))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MESA_overlay control client')
    parser.add_argument('--info', action='store_true', help='Print info from socket')
    parser.add_argument('--socket', '-s', type=str, help='Path to socket')

    commands = parser.add_subparsers(help='commands to run', dest='cmd')
    commands.add_parser('start-capture')
    commands.add_parser('stop-capture')

    args = parser.parse_args()

    control(args)
