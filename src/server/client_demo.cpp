// Note: Do not use C++ stdandard library.
//
// Do not use string, vector, list, map, thread, mutex, unique_ptr, shared_ptr!
//
// Do not use static (global, classes or functions) variables with initializers.
// Uninitialized ones should be fine.
//
// For memory allocation use malloc/free, or provide own implementation
// of new and delete.
//
// Don't throw or handle exceptions.
//
// This is to ensure we don't dynamically link with specific version
// of C++ library, which might be incompatible with C++ ABI used in
// the target application.
//
// Using templates, classes and inheritances should be fine.
//
//
// Build script will ensure we don't link to C++ runtime or use exceptions.
//
// This is a demo, so using C++ is fine.
//
// But the common.c should stay pure C.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <signal.h>
#include <cassert>
#include <arpa/inet.h>
#include <time.h>

#define PB_ENABLE_MALLOC
#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "mangohud.pb.h"

//#define SOCKET_NAME "/tmp/9Lq7BNBnBycd6nxy.socket"

#include "common.h"

void* operator new (size_t size) {
    return malloc(size);
}
void * operator new[] (size_t size) {
    return malloc(size);
}
void operator delete (void* ptr) noexcept {
    free(ptr);
}
void operator delete[] (void* ptr) noexcept {
    free(ptr);
}

static int client_response_handler(const Message* const message, void* my_state) {
    struct ClientState *client_state = (struct ClientState *)(my_state);

    if (message->protocol_version) {
        fprintf(stderr, "got response: %d\n", *(message->protocol_version));
    }
    return 0;
}

static int loop() {
retry:
    fprintf(stderr, "Connecting to server\n");

    struct ClientState client_state_ = {0};
    struct ClientState *client_state = &client_state_;

    if (client_connect(&client_state_)) {
        goto error_1;
    }

    {
    int loops = 0;

    for (;;) {
        if (client_state->response == NULL) {
            Message *request = (Message*)calloc(1, sizeof(Message));
            PB_MALLOC_SET(request->protocol_version, 1);
            PB_MALLOC_SET(request->pid, getpid());
            PB_MALLOC_SET(request->uid, getuid());
            PB_MALLOC_SET(request->fps, 50.0f + 20.0f*drand48());
            fprintf(stderr, "fps: %f\n", *(request->fps));

            PB_MALLOC_SET_STR(request->program_name, "client_demo");

            int frametimes_count = 100;
            PB_MALLOC_ARRAY(request->frametimes, 100);
            for (int i = 0; i < frametimes_count; i++) {
                 PB_MALLOC_SET(request->frametimes[i].time, abs(rand()));
            }

            client_state->response = request;
        }

        int retries = 0;
        int ret = -1;
        while ((ret = use_fd(client_state, &client_response_handler, (void*)(client_state))) == 0) {
            retries++;
            if (retries >= 3) {
                break;
            }
        }

        if (ret) {
            fprintf(stderr, "Non zero return from use_fd: %d\n", ret);
            break;
        }

        loops++;
        if (loops > 200) {
            break;
        }

        {
        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = 100000000l;  // 100ms
        nanosleep(&req, /*rem=*/NULL);
        }


    }  // for (;;)

    fprintf(stderr, "output of infinite loop\n");
    client_state_cleanup(client_state);

    }  // scoping block to silence goto warnings.

error_1:
    fprintf(stderr, "error_1 condition\n");

    if (client_state->fsocket) {
        if (fclose(client_state->fsocket) != 0) {
            perror("close");
        }
    }
    client_state->connected = 0;

    return 0;
}

#include <memory>

int main(int argc, const char** argv) {

/*
    struct sigaction act_sigpipe = {0};
    act_sigpipe.sa_handler = SIG_IGN;
    struct sigaction oldact_sigpipe;
    if (sigaction(SIGPIPE, &act_sigpipe, &oldact_sigpipe) < 0) {
        perror("sigaction: SIGPIPE");
        return 1;
    }
*/

    // pthread_sigmask maybe to only do that in one thread?

    int ret = loop();

    if (ret != 0) {
        return 1;
    }

    return 0;
}
