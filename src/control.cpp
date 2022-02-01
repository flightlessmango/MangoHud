#include <assert.h>
#include <cerrno>
#include <cstring>
#include "mesa/util/os_socket.h"
#include "overlay.h"
#ifdef MANGOAPP
#include "app/mangoapp.h"
#endif

using namespace std;
static void parse_command(struct instance_data *instance_data,
                          const char *cmd, unsigned cmdlen,
                          const char *param, unsigned paramlen)
{
    if (!strncmp(cmd, "hud", cmdlen)) {
#ifdef MANGOAPP
      {
         std::lock_guard<std::mutex> lk(mangoapp_m);
         instance_data->params.no_display = !instance_data->params.no_display;
      }
      mangoapp_cv.notify_one();
#else
      instance_data->params.no_display = !instance_data->params.no_display;
#endif
    }
    if (!strncmp(cmd, "logging", cmdlen)) {
      auto now = Clock::now(); /* us */
      if (logger->is_active())
         logger->stop_logging();
      else
         logger->start_logging();

    }
}

#define BUFSIZE 4096

/**
 * This function will process commands through the control file.
 *
 * A command starts with a colon, followed by the command, and followed by an
 * option '=' and a parameter.  It has to end with a semi-colon. A full command
 * + parameter looks like:
 *
 *    :cmd=param;
 */
static void process_char(struct instance_data *instance_data, char c)
{
   static char cmd[BUFSIZE];
   static char param[BUFSIZE];

   static unsigned cmdpos = 0;
   static unsigned parampos = 0;
   static bool reading_cmd = false;
   static bool reading_param = false;

   switch (c) {
   case ':':
      cmdpos = 0;
      parampos = 0;
      reading_cmd = true;
      reading_param = false;
      break;
   case ';':
      if (!reading_cmd)
         break;
      cmd[cmdpos++] = '\0';
      param[parampos++] = '\0';
      parse_command(instance_data, cmd, cmdpos, param, parampos);
      reading_cmd = false;
      reading_param = false;
      break;
   case '=':
      if (!reading_cmd)
         break;
      reading_param = true;
      break;
   default:
      if (!reading_cmd)
         break;

      if (reading_param) {
         /* overflow means an invalid parameter */
         if (parampos >= BUFSIZE - 1) {
            reading_cmd = false;
            reading_param = false;
            break;
         }

         param[parampos++] = c;
      } else {
         /* overflow means an invalid command */
         if (cmdpos >= BUFSIZE - 1) {
            reading_cmd = false;
            break;
         }

         cmd[cmdpos++] = c;
      }
   }
}

static void control_send(struct instance_data *instance_data,
                         const char *cmd, unsigned cmdlen,
                         const char *param, unsigned paramlen)
{
   unsigned msglen = 0;
   char buffer[BUFSIZE];

   assert(cmdlen + paramlen + 3 < BUFSIZE);

   buffer[msglen++] = ':';

   memcpy(&buffer[msglen], cmd, cmdlen);
   msglen += cmdlen;

   if (paramlen > 0) {
      buffer[msglen++] = '=';
      memcpy(&buffer[msglen], param, paramlen);
      msglen += paramlen;
      buffer[msglen++] = ';';
   }

   os_socket_send(instance_data->control_client, buffer, msglen, 0);
}

static void control_send_connection_string(struct device_data *device_data)
{
   struct instance_data *instance_data = device_data->instance;

   const char *controlVersionCmd = "MesaOverlayControlVersion";
   const char *controlVersionString = "1";

   control_send(instance_data, controlVersionCmd, strlen(controlVersionCmd),
                controlVersionString, strlen(controlVersionString));

   const char *deviceCmd = "DeviceName";
   const char *deviceName = device_data->properties.deviceName;

   control_send(instance_data, deviceCmd, strlen(deviceCmd),
                deviceName, strlen(deviceName));

   const char *mesaVersionCmd = "MesaVersion";
   const char *mesaVersionString = "Mesa";

   control_send(instance_data, mesaVersionCmd, strlen(mesaVersionCmd),
                mesaVersionString, strlen(mesaVersionString));

}

void control_client_check(struct device_data *device_data)
{
   struct instance_data *instance_data = device_data->instance;

   /* Already connected, just return. */
   if (instance_data->control_client >= 0)
      return;

   int socket = os_socket_accept(instance_data->params.control);
   if (socket == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED)
         fprintf(stderr, "ERROR on socket: %s\n", strerror(errno));
      return;
   }

   if (socket >= 0) {
      os_socket_block(socket, false);
      instance_data->control_client = socket;
      control_send_connection_string(device_data);
   }
}

static void control_client_disconnected(struct instance_data *instance_data)
{
   os_socket_close(instance_data->control_client);
   instance_data->control_client = -1;
}

void process_control_socket(struct instance_data *instance_data)
{
   const int client = instance_data->control_client;
   if (client >= 0) {
      char buf[BUFSIZE];

      while (true) {
         ssize_t n = os_socket_recv(client, buf, BUFSIZE, 0);

         if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
               /* nothing to read, try again later */
               break;
            }

            if (errno != ECONNRESET)
               fprintf(stderr, "ERROR on connection: %s\n", strerror(errno));

            control_client_disconnected(instance_data);
         } else if (n == 0) {
            /* recv() returns 0 when the client disconnects */
            control_client_disconnected(instance_data);
         }

         for (ssize_t i = 0; i < n; i++) {
            process_char(instance_data, buf[i]);
         }

         /* If we try to read BUFSIZE and receive BUFSIZE bytes from the
          * socket, there's a good chance that there's still more data to be
          * read, so we will try again. Otherwise, simply be done for this
          * iteration and try again on the next frame.
          */
         if (n < BUFSIZE)
            break;
      }
   }
} 