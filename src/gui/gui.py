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

connect_button = builder.get_object("connect_button")

SUPPORTED_PROTOCOL_VERSION = 1

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
    sock.send(serialized_size.to_bytes(4, 'big'))  # Convert to network order.
    sock.send(serialized)

def recv(sock):
    header = sock.recv(4)
    size = int.from_bytes(header, 'big')  # or use socket.ntohl ?
    data = sock.recv(size)
    msg = pb.Message()
    msg.ParseFromString(data)
    return msg

thread = None
stop = False

stop_ev = threading.Event()


@Gtk.Template(filename='client_template.glade')
class ClientWidget(Gtk.Grid):
    __gtype_name__ = 'ClientWidget'

    #def __init__(self, **kwargs):
    #    super().__init__(**kwargs)

    #@Gtk.Template.Callback('clicked')
    #def on_button_clicked(self, widget):
    #     pass

    fps = Gtk.Template.Child('fps')
    app_name = Gtk.Template.Child('app_name')
    api = Gtk.Template.Child('api')

# The key is (nodename, pid) => (ClientWidget, client last Message) with instanciated template.
known_clients = {}

clients_container = builder.get_object('clients_container')

last_row = None
last_row_count = 0

def handle_message(msg):
    global known_clients
    global last_row, last_row_count

    if msg.clients:
        new_clients = False
        for client in msg.clients:
            key = (client.nodename, client.pid)
            if key not in known_clients:
                client_widget = ClientWidget()

                #if last_row:
                if True:
                    # This probably is not safe to do from this thread
                    # clients_container.insert_next_to(last_row, Gtk.PositionType.BOTTOM)
                    clients_container.attach(client_widget, left=0, top=last_row_count+1, width=1, height=1)

                    last_row = client_widget
                    last_row_count += 1
                    known_clients[key] = [client_widget, client]
                    new_clients = True
            else:
                known_clients[key][1] = client
        if new_clients:
            clients_container.show_all()

        # TODO(baryluk): Remove stale clients or once we know for
        # sure are down.

        for key, (client_widget, client) in known_clients.items():
            GLib.idle_add(client_widget.fps.set_text, f"{client.fps:.3f}")
            GLib.idle_add(client_widget.app_name.set_text, f"{client.program_name}")
            GLib.idle_add(client_widget.api.set_text, f"pid {client.pid}")
            # TODO(baryluk): Garbage collect old clients.


def thread_loop(sock):
    protocol_version_warning_shown = False
    while not stop and not stop_ev.is_set():
        msg = pb.Message(protocol_version=1, client_type=pb.ClientType.GUI)
        send(sock, msg)

        msg = recv(sock)
        #print(msg)

        if (msg.protocol_version and msg.protocol_version > SUPPORTED_PROTOCOL_VERSION):
            if not protocol_version_warnings_shown:
                print(f"Warning: Server speaks newer protocol_version {msg.protocol_version}, than supported by this app ({SUPPORTED_PROTOCOL_VERSION}).\nCrashes or missing functionality expected!\nPlease upgrade!");
                protocol_version_warnings_shown = True

        handle_message(msg)

        # Sleep less if 50ms laready passed from previous contact.
        # Sleep more if there are no clients, to conserve CPU / battery.
        time.sleep(0.05)

def thread_loop_start(sock):
    print("Connected")
    # TODO(baryluk): Show "Connected" first, then after few seconds fade to "Disconnect".
    GLib.idle_add(connect_button.set_label, "Disconnect")
    thread_loop(sock)

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
                    # TODO(baryluk): This is too simplistic. It is still
                    # possible to connect, yet be disconnected after parsing the
                    # first message, and do reconnecting in fast loop.
                    # Improve fallback, i.e. only reset it to initial value,
                    # if few second passed and at least few messages were
                    # exchanged.
                    reconnect_delay = 1.0
                    thread_loop_start(sock)
                sock.close()
            else:
                print(f"Connecting to {SOCKET_NAME}")
                with socket.socket(family=socket.AF_UNIX, type=socket.SOCK_STREAM | socket.SOCK_CLOEXEC) as sock:
                    sock.connect(SOCKET_NAME)
                    reconnect_delay = 1.0  # See comment above for TCP.
                    thread_loop_start(sock)
                sock.close()
            status = ""
        except BrokenPipeError as e:
            status = "Broken pipe to server"
        except ConnectionRefusedError as e:
            status = "Connection refused to server (is it down?)"
        except NameError as e:
            print("Internal error")
            status = "Error"
            stop = True
            stop_ev.set()
            # raise e  # Some code bug.
            GLib.idle_add(connect_button.set_label, "Error")
            raise e
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
                if status == "Error":
                    GLib.idle_add(connect_button.set_label, "Error!")
                    # raise NameError()
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
        # TODO: If explicitly clicked while we are in "Reconnecting in {...}"
        # phase. Force reconnect.
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

try:
  window.show_all()

  # Auto connect on startup.
  connect_clicked(connect_button)

  Gtk.main()
finally:
  stop = True
  stop_ev.set()
