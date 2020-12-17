#pragma once
#ifndef _MANGO_SERVER_COMMON_H_
#define _MANGO_SERVER_COMMON_H_

// Note. We leave mangohud.pb.h outside of extern "C", becasue
// it includes pb.h, which has a bit of C++ code when in __cplusplus
// mode. Let it be, it is harmless and actualy useful.

#define PB_ENABLE_MALLOC
//#include <pb.h>
//#include <pb_encode.h>
//#include <pb_decode.h>
#include "mangohud.pb.h"


#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>


// Helpers for manipulating fields in protocol buffer messages.
// Can be used by both client and server.
//
// Examples:
//            Message request = {0};
//            MALLOC_SET(request.protocol_version, 1);
//            MALLOC_SET(request.pid, getpid());
//            MALLOC_SET(request.uid, getuid());
//            MALLOC_SET(request.fps, 54.123);
//
//            int frametimes_count = 100;
//            MALLOC_ARRAY(request.frametimes, 100);
//            for (int i = 0; i < frametimes_count; i++) {
//                 MALLOC_SET(request.frametimes[i].time, abs(rand()));
//            }
//
//            MALLOC_SET(request.rander_info, RenderInfo_init_zero);
//            MALLOC_SET(request.rander_info->vulkan, true);
//
//            pb_release(&Message_fields, &request);
//
// These can be used only on fields marked as FT_POINTER (using nanopb
// annotations).
//
// This restriction might be lifted in the future.

//#define MALLOC_SET(field, value) do { (field) = (__typeof__(field))malloc(sizeof(field)); *(field) = (value); } while (0)
#define MALLOC_SET(field, value) do { (field) = (__typeof__(field))malloc(sizeof(*(field))); *(field) = value; } while (0)
#define MALLOC_ARRAY(field, count) do { const size_t __cc = (count); (field) = (__typeof__(field))calloc((__cc), sizeof(*(field))); field ## _count = (__cc); } while (0)

// Note: On Linux, the length must be less than 108 bytes
// (including a NULL terminator).
// Some implementations have it as short as 92 bytes.
//
// TODO(baryluk): Use dynamic name with uid / user.
#define SOCKET_NAME "/tmp/mangohud_server.socket"

#define MUST_USE_RESULT __attribute__ ((warn_unused_result))
#define COLD __attribute__ ((cold))
#define NOTNULL __attribute__ ((nonnull))
#define NOTHROW __attribute__ ((nothrow))

// Client state. It is used by both server and 'client', for tracking various
// things.
//
// Applications (especially clients), should not be reading or writing, any
// of the state, unless it is for debug / tracing purposes.
//
// Use provided functions instead.
struct ClientState {
    int client_type;  // 0 - server, 1 - app, 2 - gui.

    uint64_t next_retry;
    uint64_t last_connect_try;

    // If 0, then we are not connected, and don't have a socket yet.
    int fd;
    // If NULL, the maybe we do have socket, but we didn't finish connecting.
    FILE* fsocket;
    // We use FILE, so we can do buffering on reads, reducing syscall load.
    // `fsocket` uses `fd`. When closing, fclose `fsocket` if it exists,
    // otherwise close `fd`. Never both.

    // If true, then we are fully connected.
    int connected;

    // State of the client. Any combination is permited.
    int in_sending;
    int in_receiving;

    // Buffer for receving framing (header) information.
    // Header at the moment stores just the size of the subsequent
    // serialized message. On wire the 4-byte unsigned integer,
    // in network order.
    size_t input_frame_buffer_length;
    union {
        uint8_t input_frame_buffer[4];
        // Note: Only use using `ntohl`.
        uint32_t input_frame_buffer_uint32;
    };

    // Decoded size from frame header.
    // This is only valid to use, if `input_frame_buffer_length` == 4.
    size_t input_serialized_size;

    // Buffer for receving serialized data. Once fully received
    // this will be deserialized (potentially in place).
    //
    // The buffer is never freed (other than when closing client),
    // instead it is reallocated if needed and reused.
    //
    // `input_data_buffer_size` is bytes received so far.
    size_t input_data_buffer_size;
    size_t input_data_buffer_capacity;
    uint8_t* input_data_buffer;

    // Similar for output. The buffer is never freed (other than when
    // closing client), instead it is reallocated if needed and reused.
    size_t output_data_buffer_size;
    size_t output_data_buffer_capacity;
    uint8_t* output_data_buffer;

    // For output and keeping track of how much more to send.
    ssize_t output_serialized_size;
    ssize_t output_send_remaining;
    ssize_t output_sent_already;

    // At the moment we only support one in-flight message being sent out.
    // New message will only be generated once we are done
    // with previous message (i.e. it was serialized AND fully sent).
    //
    // In the future we might make a queue for multiple in-flight messages,
    // with flow control.
    //
    // As also ability to have multiple requests and responses in flight,
    // identified by pointers and rcp_id.
    Message* response;

    // Used for rate limiting requests, that we wish to be sending on our own.
    // TODO(baryluk): Move this to the message_generator instead.
    int last_send_time;
    int send_period;
};

int client_connect(struct ClientState *client_state) MUST_USE_RESULT COLD;

void client_state_cleanup(struct ClientState *client_state) COLD;

// Used internally in client_connect in the server.
int set_nonblocking(int fd) MUST_USE_RESULT COLD;

//int protocol_receive(struct ClientState *client_state, int(*request_handler)(const Message*, void*), void *request_handler_state);
//int protocol_send(struct ClientState *client_state);

int use_fd(struct ClientState *client_state, int(*request_handler)(const Message*, void*), void *request_handler_state) MUST_USE_RESULT;

// Any parameter can be NULL, but to be useful, client_state should be not NULL.
// If message_handler is NULL, messages from other side will be ignored.
//
// In general this function is safe and fast to call, even if there is nothing
// to send or receive, or the client is not connected.
//
// Calls to message_generator will be throttled, and not necassirly
// invoked, even if we are ready to send.
//
// Message pointer passed to `message_handler` is only valid, during
// the call of `message_handler`. The message pointer and all the other
// pointers referenced transitively will be invalid after `message_handler`
// finishes. Make copies of data, if needed.
void client_maybe_communicate(struct ClientState *client_state,
                              int(*message_generator)(Message*, void*),
                              void *generator_state,
                              int(*message_handler)(const Message*, void*),
                              void *handler_state);

#undef MUST_USE_RESULT
#undef COLD
#undef NOTNULL
#undef NOTHROW

#ifdef __cplusplus
}
#endif

#endif
