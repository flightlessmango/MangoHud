#include <string.h>
#include <stdio.h>
#include "mangoapp.h"

void help_and_quit() {
    // TODO! document attributes and values
    fprintf(stderr, "Usage: mangohudctl [set|toggle] attribute [value]\n");
    fprintf(stderr, "Attributes:\n");
    fprintf(stderr, "   no_display      hides or shows hud\n");
    fprintf(stderr, "   log_session     handles logging status\n");
    fprintf(stderr, "Accepted values:\n");
    fprintf(stderr, "   true\n");
    fprintf(stderr, "   false\n");
    fprintf(stderr, "   1\n");
    fprintf(stderr, "   0\n");
    exit(1);
}

bool str_to_bool(const char *value)
{
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0)
        return true;
    else if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0)
        return false;
    
    /* invalid boolean, display a nice error message saying that */
    fprintf(stderr, "The value '%s' is not an accepted boolean. Use 0/1 or true/false\n", value);
    exit(1);
}

bool set_attribute(struct mangoapp_ctrl_msgid1_v1 *ctrl_msg,
                   const char *attribute, const char* value)
{
    if (strcmp(attribute, "no_display") == 0) {
        ctrl_msg->no_display = str_to_bool(value) ? 1 : 2;
        return true;
    } else if (strcmp(attribute, "log_session") == 0) {
        ctrl_msg->log_session = str_to_bool(value) ? 1 : 2;
        return true;
    } 
    
    return false;
}

bool toggle_attribute(struct mangoapp_ctrl_msgid1_v1 *ctrl_msg,
                      const char *attribute)
{
    if (strcmp(attribute, "no_display") == 0) {
        ctrl_msg->no_display = 3;
        return true;
    } else if (strcmp(attribute, "log_session") == 0) {
        ctrl_msg->log_session = 3;
        return true;
    } 
    
    return false;
}

int main(int argc, char *argv[])
{
    /* Set up message queue */
    int key = ftok("mangoapp", 65);
    int msgid = msgget(key, 0666 | IPC_CREAT);
    /* Create the message that we will send to mangohud */
    struct mangoapp_ctrl_msgid1_v1 ctrl_msg = {0};
    ctrl_msg.hdr.msg_type = 2;
    ctrl_msg.hdr.ctrl_msg_type = 1;
    ctrl_msg.hdr.version = 1;
    
    if (argc <= 2)
        help_and_quit();
    
    if (strcmp(argv[1], "set") == 0) {
        if (argc != 4)
            help_and_quit();

        set_attribute(&ctrl_msg, argv[2], argv[3]);
    } else if (strcmp(argv[1], "toggle") == 0) {
        if (argc != 3)
            help_and_quit();

        toggle_attribute(&ctrl_msg, argv[2]);
    } else
        help_and_quit();
        
    msgsnd(msgid, &ctrl_msg, sizeof(mangoapp_ctrl_msgid1_v1), IPC_NOWAIT);
        
    return 0;
}