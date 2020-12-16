#!/usr/bin/env python3

import sys
import os

import gi

import protos.mangohud_pb2 as pb

msg = pb.Message()
msg.protocol_version = 1
print(msg)
print(msg.SerializeToString())

data = bytes()

msg_in = pb.Message()
msg_in.ParseFromString(data)



gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib, Gdk, Gio

screen = Gdk.Screen.get_default()
gtk_provider = Gtk.CssProvider()

gtk_context = Gtk.StyleContext
#gtk_context.add_provider_for_screen(screen, gtk_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

#css = b""".bigone { font-size: 32px; font-weight: bold; }"""
#gtk_provider.load_from_data(css)
css_file = Gio.File.new_for_path("gui.css")
gtk_provider.load_from_file(css_file)

gtk_context.add_provider_for_screen(screen, gtk_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)


builder = Gtk.Builder()
builder.add_from_file("gui.glade")

# builder.add_objects_from_file("example.glade", ("button1", "button2"))

import threading
import time

fps_label = builder.get_object("fps")
app_name_label = builder.get_object("app_name")
api_label = builder.get_object("api")

SOCKET_NAME = "/tmp/9Lq7BNBnBycd6nxy.socket"

import socket

def send(sock, msg):
    serialized = msg.SerializeToString()
    serialized_size = len(serialized)
    sock.send(serialized_size.to_bytes(4, 'big'))
    sock.send(serialized)

def recv(sock):
    header = sock.recv(4)
    size = int.from_bytes(header, 'big')
    # size = socket.ntohl(header)
    data = sock.recv(size)
    msg = pb.Message()
    msg.ParseFromString(data)
    return msg

thread = None

def connection_thread():
    global thread
    with socket.socket(family=socket.AF_UNIX, type=socket.SOCK_STREAM | socket.SOCK_CLOEXEC) as sock:
        sock.connect(SOCKET_NAME)

        while True:
            msg = pb.Message(protocol_version=1, client_type=pb.ClientType.GUI)
            send(sock, msg)

            msg = recv(sock)
            print(msg)

            fps = msg.fps
            GLib.idle_add(fps_label.set_text, f"{fps:.3f}")
            program_name = msg.program_name
            GLib.idle_add(app_name_label.set_text, f"{program_name}")
            GLib.idle_add(api_label.set_text, f"")

            time.sleep(0.05)
        sock.close()
    print("Disconnected")
    thread = None

def connect(button):
    global thread
    if thread:
        print("Already connected")
        return
    print("Connecting...")
    # button.label.set_text("Connecting...")
    thread = threading.Thread(target=connection_thread)
    thread.daemon = True
    thread.start()

handlers = {
  "connect_clicked": connect,
}

builder.connect_signals(handlers)

window = builder.get_object("window_main")
window.show_all()




Gtk.main()
