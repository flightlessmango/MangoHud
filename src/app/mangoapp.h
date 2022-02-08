#include <stdint.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <mutex>
#include <condition_variable>

extern int ctrl_msgid;
extern int msgid;
extern bool mangoapp_paused;
extern std::mutex mangoapp_m;
extern std::condition_variable mangoapp_cv;
struct mangoapp_msg_header {
    long msg_type;  // Message queue ID, never change
    uint32_t version;  /* for major changes in the way things work */
} __attribute__((packed));

struct mangoapp_msg_v1 {
    struct mangoapp_msg_header hdr;
    
    uint32_t pid;
    uint64_t frametime_ns;
    uint8_t fsrUpscale;
    uint8_t fsrSharpness;
    // WARNING: Always ADD fields, never remove or repurpose fields
} __attribute__((packed));

struct mangoapp_ctrl_header {
    long msg_type;  // Message queue ID, never change
    uint32_t ctrl_msg_type; /* This is a way to share the same thread between multiple types of messages */
    uint32_t version;  /* version of the message type, for backwards incompatible changes */
} __attribute__((packed));

struct mangoapp_ctrl_msgid1_v1 {
    struct mangoapp_ctrl_header hdr;
    
    // When a field is set to 0, it should always mean "ignore" or "no changes"
    uint8_t no_display;      // 0x0 = ignore; 0x1 = disable; 0x2 = enable; 0x3 = toggle
    uint8_t log_session;     // 0x0 = ignore; 0x1 = start a session; 0x2 = stop the current session; 0x3 = toggle logging
    char log_session_name[64]; // if byte 0 is NULL, ignore. Needs to be set when starting/toggling a session if we want to override the default name
    
    // WARNING: Always ADD fields, never remove or repurpose fields
} __attribute__((packed));

extern uint8_t g_fsrUpscale;
extern uint8_t g_fsrSharpness;