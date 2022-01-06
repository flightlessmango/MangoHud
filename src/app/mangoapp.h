#include <stdint.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <mutex>
#include <condition_variable>

extern struct mangoapp_ctrl_msg_v1 mangoapp_ctrl;
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
    // WARNING: Always ADD fields, never remove or repurpose fields
} __attribute__((packed));

struct mangoapp_ctrl_msg_v1 {
    struct mangoapp_msg_header hdr;

    uint8_t ready_for_updates;
    // WARNING: Always ADD fields, never remove or re-purpose fields
} __attribute__((packed));