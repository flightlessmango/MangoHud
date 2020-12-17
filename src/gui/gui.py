#!/usr/bin/env python3

import sys
import os

import gi

import protos.mangohud_pb2 as pb

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib, Gdk, Gio, GObject

screen = Gdk.Screen.get_default()
gtk_provider = Gtk.CssProvider()

gtk_context = Gtk.StyleContext
#gtk_context.add_provider_for_screen(screen, gtk_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
#css = b""".bigone { font-size: 32px; font-weight: bold; }"""
#gtk_provider.load_from_data(css)  # For some reasons, it doesn't work.
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

connect_button = builder.get_object("connect_button")

ADDRESS = ("localhost", 9869)

# The length verification to system limits, will be checked by Python wrapper
# in `sock.connect`.
if os.getuid:
    SOCKET_NAME = f"/tmp/mangohud_server-{os.getuid()}.socket"
else:
    # For Windows.
    SOCKET_NAME = f"/tmp/mangohud_server.socket"

if "MANGOHUD_SERVER" in os.environ:
    SOCKET_NAME = os.environ["MANGOHUD_SERVER"]

# TODO(baryluk): Automatically determine type of server (UNIX vs TCP),
# from MANGOHUD_SERVER variable.

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
stop = False

stop_ev = threading.Event()

def thread_loop(sock):
    while not stop and not stop_ev.is_set():
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

def connection_thread():
    global thread, stop, stop_ev
    status = "Connecting"
    reconnect = True
    reconnect_delay = 1.0
    while reconnect and not stop and not stop_ev.is_set():
        try:
            if True:
                addresses = socket.getaddrinfo(ADDRESS[0], ADDRESS[1], proto=socket.IPPROTO_TCP)
                assert addresses
                address = addresses[0]  # (family, type, proto, canonname, sockaddr)
                family, type_, proto, canonname, sockaddr = address
                assert type_ == socket.SOCK_STREAM
                print(f"Connecting to {address}")
                with socket.socket(family=family, type=socket.SOCK_STREAM | socket.SOCK_CLOEXEC, proto=proto) as sock:
                    sock.connect(sockaddr)
                    GLib.idle_add(connect_button.set_label, "Disconnect")
                    reconnect_delay = 1.0
                    thread_loop(sock)
                sock.close()
            else:
                print(f"Connecting to {SOCKET_NAME}")
                with socket.socket(family=socket.AF_UNIX, type=socket.SOCK_STREAM | socket.SOCK_CLOEXEC) as sock:
                    sock.connect(SOCKET_NAME)
                    GLib.idle_add(connect_button.set_label, "Disconnect")
                    reconnect_delay = 1.0
                    thread_loop(sock)
                sock.close()
            status = ""
        except BrokenPipeError as e:
            status = "Broken pipe to server"
        except ConnectionRefusedError as e:
            status = "Connection refused to server (is it down?)"
        finally:
            print(f"Disconnected: status = {status}")
            if not stop and not stop_ev.is_set():
                reconnect_time = time.time() + reconnect_delay
                while time.time() < reconnect_time:
                    reconnect_togo = max(0.0, reconnect_time - time.time())
                    GLib.idle_add(connect_button.set_label, f"Reconnecting in {reconnect_togo:.1f}s")
                    time.sleep(0.1)
                reconnect_delay = min(30.0, reconnect_delay * 1.4)
                GLib.idle_add(connect_button.set_label, "Reconnecting...")
            else:
                GLib.idle_add(connect_button.set_label, "Connect")
    thread = None
    stop = False
    stop_ev.clear()

def connect_clicked(button):
    global thread
    if connect_button.get_label() == "Disconnect":
        stop = True
        stop_ev.set()
        return

    if thread:
        print("Already connected or connect in progress")
        # TODO: If explicitly clicked while we are in "Reconnecting in {...}" phase. Force reconnect.
        return

    print("Connecting...")
    # button.label.set_text("Connecting...")
    GLib.idle_add(connect_button.set_label, "Connecting...")
    thread = threading.Thread(target=connection_thread)
    # thread.daemon = True  # This means to not wait for the thread on exit. Just kill it.
    thread.start()

handlers = {
  "connect_clicked": connect_clicked,
}

builder.connect_signals(handlers)

window = builder.get_object("window_main")

window.connect("destroy", Gtk.main_quit)

window.show_all()



Gtk.main()
stop = True
stop_ev.set()
