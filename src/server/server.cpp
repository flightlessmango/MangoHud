#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <assert.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <signal.h>
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <cinttypes>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifndef PB_ENABLE_MALLOC
#define PB_ENABLE_MALLOC
#endif
#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "mangohud.pb.h"

// Note, the connection can be established for debugging using socat:
// socat -d -d -d unix-connect:/tmp/9Lq7BNBnBycd6nxy.socket,type=5 stdio

//#define SOCKET_NAME "/tmp/9Lq7BNBnBycd6nxy.socket"

#define MAX_EVENTS 10

#include "common.h"


template<typename T>
inline T* a(const T&& value) {
  T* ptr = (T*)malloc(sizeof(T));
  *ptr = value;
  return ptr;
}

// This is per client.
struct ServerState {
   int server_states_index;  // Index in `server_states` vector.

   struct ClientState client_state;

   Message recent_state;
};

struct RequestContext {
  // Server state assosciated with particular client.
  struct ServerState *server_state;

  // All server states.
  //
  // We want to be able to access all the other clients easily
  // to server GUI type client.
  std::vector<ServerState*> *all_server_states;
};

#define UPDATE(to, from) do { \
  if (from) { \
     if (!(to)) { \
         (to) = (__typeof__(to))malloc(sizeof(*(to))); \
     } \
     *(to) = *(from); \
  } \
} while (0)

#define UPDATE_STR(to, from) do { \
  if (from) { \
     if (to) { \
         free(to); \
     } \
     to = strdup(from); \
  } \
} while (0)

#define IF(field, value) ((field) && *(field) == (value))

static int server_request_handler(const Message* const request, void* my_state) {
    // This is a bit circular, and not nice design, but should work.
    struct RequestContext *const context = (struct RequestContext*)my_state;

    struct ServerState *const server_state = context->server_state;
    assert(server_state != NULL);

    // Debugging / sanity checks.
    // assert(server_state->client_state.connected);

    //if (request->protocol_version) {
    //    fprintf(stderr, "protocol_version from client: %" PRIu32 "\n", *(request->protocol_version));
    //}

    uint64_t pid = 0;
    if (request->pid) {
        //fprintf(stderr, "   pid: %" PRIu64 "\n", *request->pid);
        pid = *request->pid;
    }
    if (request->uid) {
        //fprintf(stderr, "   uid: %" PRIu64 "\n", *request->uid);
    }
    if (request->fps) {
        char* type = "unknown";
        if (request->render_info && request->render_info->opengl && *request->render_info->opengl) {
            type = "OpenGL";
        }
        if (request->render_info && request->render_info->vulkan && *request->render_info->vulkan) {
            type = "Vulkan";
        }
        char* engine_name = "";
        if (request->render_info && request->render_info->engine_name) {
            engine_name = request->render_info->engine_name;
        }
        char* driver_name = "";
        if (request->render_info && request->render_info->vulkan_driver_name) {
            driver_name = request->render_info->vulkan_driver_name;
        }
        
        fprintf(stderr, "pid %9ld   fps: %.3f  name=%s  type=%s engine=%s driver=%s\n", pid, *request->fps, request->program_name, type, engine_name, driver_name);

        UPDATE_STR(server_state->recent_state.program_name, request->program_name);
        UPDATE(server_state->recent_state.fps, request->fps);
    }

    UPDATE(server_state->recent_state.client_type, request->client_type);

    std::vector<uint32_t> frametimes;
    if (request->frametimes) {
        for (int i = 0; i < request->frametimes_count; i++) {
            if (request->frametimes[i].time) {
               frametimes.push_back(*(request->frametimes[i].time));
            }
        }
    }
    for (auto& frametime : frametimes) {
        fprintf(stderr, "   frame: %" PRIu32 "\n", frametime);
    }

    // Be very careful what you are doing here.
    // Don't mess with client_state, only set response if it is NULL.
    // Don't touch anything else (even reading).
    struct ClientState *client_state = &(server_state->client_state);
    if (client_state->response == NULL) {
        Message* response = (Message*)calloc(1, sizeof(Message));
        client_state->response = response;

        response->protocol_version = a<uint32_t>(112128371 + rand());

        if (IF(request->client_type, ClientType_GUI)) {
           for (auto& other_server_state : *(context->all_server_states)) {
               if (server_state->server_states_index == other_server_state->server_states_index) {
                   continue;
               }
               if (IF(other_server_state->recent_state.client_type, ClientType_GUI)) {
                   continue;
               }

               UPDATE_STR(response->program_name, other_server_state->recent_state.program_name);
               UPDATE(response->fps, other_server_state->recent_state.fps);

               break;
           }
        }
    } else {
        fprintf(stderr, "Can not respond, some response is already set!\n");
    }

    return 0;
}

// TODO(baryluk): Move this to ServerState.
static bool server_shutdown = false;

static void sigint_handler(int sig, siginfo_t *info, void *ucontext) {
    server_shutdown = true;
}

static int loop() {
    // Create local socket.

retry_unix_socket:
    // int connection_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    int connection_unix_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (connection_unix_socket == -1) {
        if (errno == EINTR) {
            goto retry_unix_socket;
        }
        return errno;  // socket
    }

    {
    struct sockaddr_un name;
    memset(&name, 0, sizeof(name));

    // Bind socket to socket name.

    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    int retry = 1;
retry_unix_bind:
    if (bind(connection_unix_socket, (const struct sockaddr *)&name,
             sizeof(name)) != 0) {
        if (errno == EADDRINUSE && retry > 0) {
            retry = 0;
            //if (connect(data_socket, (const struct sockaddr *) &addr,
            //            sizeof(addr));

            unlink(SOCKET_NAME);
            goto retry_unix_bind;
        }
        return errno;  // bind
    }
    }

// AF_INET6 or PF_INET6?
    int connection_tcp_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (connection_tcp_socket == -1) {
        if (errno == EINTR) {
            goto retry_unix_socket;
        }
        return errno;  // socket
    }

    {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(sockaddr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(9869);
    //addr.sin6_addr = IN6ADDR_ANY_INIT;
    addr.sin6_addr = in6addr_any;

    int retry = 1;
retry_tcp_bind:
    if (bind(connection_tcp_socket, (const struct sockaddr *)&addr,
             sizeof(addr)) != 0) {
        if (errno == EADDRINUSE && retry > 0) {
            retry = 0;
            //if (connect(data_socket, (const struct sockaddr *) &addr,
            //            sizeof(addr));

            goto retry_tcp_bind;
        }
        return errno;  // bind
    }
    }

    {
    int reuse = 1;
    setsockopt(connection_tcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    //{
    //int timestamp = 1;
    //setsockopt(connection_tcp_socket, SOL_SOCKET, SO_TIMESTAMP, &timestampe, sizeof(timestamp));
    //}


    if (listen(connection_unix_socket, 20) != 0) {
        return errno;  // listen
    }

    fprintf(stderr, "Listening on UNIX socket %s\n", SOCKET_NAME);


    if (listen(connection_tcp_socket, 20) != 0) {
        return errno;  // listen
    }

    {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(sockaddr));
        socklen_t addr_len = sizeof(addr);
        if (getsockname(connection_tcp_socket, (sockaddr*)&addr, &addr_len) == -1) {
            perror("getsockname");
        }
        assert(addr_len <= sizeof(addr));

        char addr_str[INET6_ADDRSTRLEN];
        addr_str[0] = '\0';
        if (inet_ntop(AF_INET6, &addr.sin6_addr, addr_str, sizeof(addr_str))) {
            fprintf(stderr, "Listening on TCP socket %s port %d\n", addr_str, ntohs(addr.sin6_port));
        } else {
            perror("inet_ntop");
        }
    }

    int epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    int unix_socket_dummy_ptr = 0;
    int tcp_socket_dummy_ptr = 0;

    {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = (void*)&unix_socket_dummy_ptr;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connection_unix_socket, &ev) == -1) {
        perror("epoll_ctl: connection_socket");
        exit(EXIT_FAILURE);
    }
    }

    {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = (void*)&tcp_socket_dummy_ptr;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connection_tcp_socket, &ev) == -1) {
        perror("epoll_ctl: connection_socket");
        exit(EXIT_FAILURE);
    }
    }

    std::vector<struct ServerState*> server_states;

    while (!server_shutdown) {
        struct epoll_event events[MAX_EVENTS];

        int nfds = epoll_pwait(epollfd, events, MAX_EVENTS, /*(int)timeout_ms=*/-1, /*sigmask*/NULL);
        if (nfds < 0 && errno == EINTR) {
            // Retry or exit if in shutdown.
            continue;
        }
        if (nfds < 0) {
            perror("epoll_pwait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; n++) {
            if (events[n].data.ptr == &unix_socket_dummy_ptr ||
                events[n].data.ptr == &tcp_socket_dummy_ptr) {

                struct sockaddr_in6 addr;
                memset(&addr, 0, sizeof(sockaddr));
                socklen_t addr_size = sizeof(addr);

                int data_socket =
                     events[n].data.ptr == &unix_socket_dummy_ptr
                     ? accept(connection_unix_socket, NULL, NULL)
                     : accept4(connection_tcp_socket, (sockaddr*)&addr, &addr_size, SOCK_CLOEXEC);

// accept4(connection_tcp_socket, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

                if (data_socket == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    if (events[n].data.ptr == &tcp_socket_dummy_ptr) {
                        if (errno == ENETDOWN || errno == EPROTO ||
                            errno == ENOPROTOOPT || errno == EHOSTDOWN ||
                            errno == ENONET || errno == EHOSTUNREACH ||
                            errno == EOPNOTSUPP || errno == ENETUNREACH) {
                            continue;
                        }
                    }
// EPERM Firewall rules forbid connection. (Linux)

                    perror("accept");
                    return errno;  // accept
                }

                fprintf(stderr, "Client connect started\n");

/*
       accept4() is a nonstandard Linux extension.

       On Linux, the new socket returned by accept() does not inherit file
       status flags such as O_NONBLOCK and O_ASYNC from the listening
       socket.  This behavior differs from the canonical BSD sockets
       implementation.  Portable programs should not rely on inheritance or
       noninheritance of file status flags and always explicitly set all
       required flags on the socket returned from accept().

*/
                if (events[n].data.ptr == &tcp_socket_dummy_ptr) {
                    char addr_str[INET6_ADDRSTRLEN];
                    addr_str[0] = '\0';
                    if (inet_ntop(AF_INET6, &addr.sin6_addr, addr_str, sizeof(addr_str))) {
                        fprintf(stderr, "TCP connection from %s port %d\n", addr_str, ntohs(addr.sin6_port));
                    } else {
                        perror("inet_ntop");
                    }
                }

                if (events[n].data.ptr == &tcp_socket_dummy_ptr) {
                    {
                    int keepalive = 1;
                    setsockopt(data_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                    }

                    {
                    struct linger no_linger;
                    no_linger.l_onoff = 0;
                    no_linger.l_linger = 0;  // seconds
                    setsockopt(data_socket, SOL_SOCKET, SO_LINGER, &no_linger, sizeof(no_linger));
                    }

                    {
                    struct sockaddr_in6 addr;
                    memset(&addr, 0, sizeof(addr));
                    socklen_t addr_len = sizeof(addr);
                    if (getpeername(data_socket, (sockaddr*)&addr, &addr_len) == -1) {
                        perror("getpeername");
                        exit(EXIT_FAILURE);
                    }
                    assert(addr_len == sizeof(struct sockaddr_in6));

                    char addr_str[INET6_ADDRSTRLEN];
                    addr_str[0] = '\0';
                    if (inet_ntop(AF_INET6, &addr.sin6_addr, addr_str, sizeof(addr_str))) {
                        fprintf(stderr, "TCP connection from %s port %d\n", addr_str, ntohs(addr.sin6_port));
                    } else {
                        perror("inet_ntop");
                    }
                    }
                }

// IP_TOS   , IPTOS_LOWDELAY



                if (set_nonblocking(data_socket)) {
                    goto error_1;
                }

                // Use extra scope to silence gcc's: jump to label ‘error_1’ crosses initialization of client_state / fsocket.
                {

                // We use libc FILE streams, for buffering.
                FILE* fsocket = fdopen(data_socket, "r");
                if (fsocket == NULL) {
                    perror("fdopen");
                    goto error_1;
                }

                struct ServerState *const server_state = (struct ServerState*)calloc(1, sizeof(struct ServerState));
                // struct ServerState *const server_state = (struct ServerState*)malloc(sizeof(struct ServerState));
                // memset(server_state, 0, sizeof(struct ServerState));

                struct ClientState *const client_state = &(server_state->client_state);

                client_state->client_type = 1;
                client_state->fd = data_socket;
                client_state->fsocket = fsocket;
                client_state->connected = 1;

                server_state->server_states_index = server_states.size();
                server_states.push_back(server_state);

                struct epoll_event ev;
                // Note that is not needed to ever set EPOLLERR or EPOLLHUP,
                // they will always be reported.
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = data_socket;
                ev.data.ptr = (void*)server_state;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, data_socket, &ev) == -1) {
                    perror("epoll_ctl: data_socket");
                    exit(EXIT_FAILURE);
                }
                }

                fprintf(stderr, "Connected clients: %ld\n", server_states.size());

                continue;
error_1:
                fprintf(stderr, "Failure during creation of connection from client\n");
                if (data_socket < 0) {
                    continue;
                }
                if (close(data_socket)) {
                    perror("close");
                }
                continue;

            } else {
                // Data from client.
                struct ServerState *const server_state = (struct ServerState*)events[n].data.ptr;
                assert(server_state != NULL);
                struct ClientState *const client_state = &(server_state->client_state);

                struct RequestContext request_context = {0};
                request_context.server_state = server_state;
                request_context.all_server_states = &server_states;

                // use_fd is fine to be called even if we got EPOLLERR or EPOLLHUP,
                // as use_fd is smart to handle properly read and write errors,
                // including returning 0, or other errors.
                if ((events[n].events & EPOLLERR) || (events[n].events & EPOLLHUP) || use_fd(client_state, server_request_handler, (void*)(&request_context))) {
                    fprintf(stderr, "Fatal error during handling client. Disconnecting.\n");
                    struct epoll_event dummy_event = {0};
                    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, client_state->fd, &dummy_event) == -1) {
                        perror("epoll_ctl: removing data_socket");
                        exit(EXIT_FAILURE);
                    }

                    // fclose is now power of client_state_cleanup.
                    //if (fclose(client_state->fsocket)) {
                    //    perror("fclose");
                    //}

                    int i = server_state->server_states_index;
                    assert(server_states.size() > 0);
                    assert(server_states[i] == server_state);
                    struct ServerState *other_server_state = server_states.back();
                    if (other_server_state != server_state) {
                        assert(server_states.size() >= 1);
                        other_server_state->server_states_index = i;
                        server_states[i] = other_server_state;
                    }
                    server_states.pop_back();
                    fprintf(stderr, "New server_states vector size: %ld\n", server_states.size());

                    client_state_cleanup(client_state);
                    client_state->connected = 0;

                    // free(server_state->client_state);
                    free(server_state);
                }
            }
        }
    }

    if (server_shutdown) {
        fprintf(stderr, "Received SIGINT (^C), shuting down\n");
    }

    // No need to do EPOLL_CTL_DEL on all connections.
    // Just close epollfd.
    if (close(epollfd) != 0) {
        perror("close: epollfd");
    }

    for (size_t i = 0; i < server_states.size(); i++) {
        printf("Closing %ld\n", i);
        client_state_cleanup(&(server_states[i]->client_state));
        // free(server_states[i]->client_state);
        free(server_states[i]);
    }
    server_states.clear();

close_tcp_socket:
    if (close(connection_tcp_socket) != 0) {
        perror("close: connection_tcp_socket");
    }

close_unix_socket:
    if (close(connection_unix_socket) != 0) {
        perror("close: connection_unix_socket");
    }

    if (unlink(SOCKET_NAME) != 0) {
        perror("unlink: unix_socket file");
    }

    return 0;
}



int main(int argc, const char** argv) {
    server_shutdown = false;

    struct sigaction act_sigint = {0};
    act_sigint.sa_sigaction = &sigint_handler;
    act_sigint.sa_flags = SA_SIGINFO;
    struct sigaction oldact_sigint;
    if (sigaction(SIGINT, &act_sigint, &oldact_sigint) < 0) {
        perror("sigaction: SIGINT");
        return 1;
    }


    struct sigaction act_sigpipe = {0};
    act_sigpipe.sa_handler = SIG_IGN;
    struct sigaction oldact_sigpipe;
    if (sigaction(SIGPIPE, &act_sigpipe, &oldact_sigpipe) < 0) {
        perror("sigaction: SIGPIPE");
        return 1;
    }

    int ret = loop();

    if (ret != 0) {
        errno = ret;
        perror("something");
        return 1;
    }

    fprintf(stderr, "Bye\n");

    return 0;
}
