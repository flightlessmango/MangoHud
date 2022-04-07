#include <assert.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include "mesa/util/os_socket.h"
#include "overlay.h"
#include "version.h"
#ifdef MANGOAPP
#include "app/mangoapp.h"
#endif

using namespace std;
static void parse_command(overlay_params &params,
                          const char *cmd, unsigned cmdlen,
                          const char *param, unsigned paramlen)
{
   if (!strncmp(cmd, "hud", cmdlen)) {
#ifdef MANGOAPP
      {
         std::lock_guard<std::mutex> lk(mangoapp_m);
         params.no_display = !params.no_display;
      }
      mangoapp_cv.notify_one();
#else
      params.no_display = !params.no_display;
#endif
   }

   if (!strncmp(cmd, "logging", cmdlen)) {
      if (param && param[0])
      {
         int value = atoi(param);
         if (!value && logger->is_active())
            logger->stop_logging();
         else if (value > 0 && !logger->is_active())
            logger->start_logging();
      }
      else
      {
         if (logger->is_active())
            logger->stop_logging();
         else
            logger->start_logging();
      }
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
static void process_char(const int control_client, overlay_params &params, char c)
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
      parse_command(params, cmd, cmdpos, param, parampos);
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

static void control_send(int control_client,
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

   os_socket_send(control_client, buffer, msglen, MSG_NOSIGNAL);
}

static void control_send_connection_string(int control_client, const std::string& deviceName)
{
   const char *controlVersionCmd = "MangoHudControlVersion";
   const char *controlVersionString = "1";

   control_send(control_client, controlVersionCmd, strlen(controlVersionCmd),
                controlVersionString, strlen(controlVersionString));

   const char *deviceCmd = "DeviceName";

   control_send(control_client, deviceCmd, strlen(deviceCmd),
                deviceName.c_str(), deviceName.size());

   const char *versionCmd = "MangoHudVersion";
   const char *versionString = "MangoHud " MANGOHUD_VERSION;

   control_send(control_client, versionCmd, strlen(versionCmd),
                versionString, strlen(versionString));

}

void control_client_check(int control, int& control_client, const std::string& deviceName)
{
   /* Already connected, just return. */
   if (control_client >= 0)
      return;

   int socket = os_socket_accept(control);
   if (socket == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED)
         fprintf(stderr, "ERROR on socket: %s\n", strerror(errno));
      return;
   }

   if (socket >= 0) {
      os_socket_block(socket, false);
      control_client = socket;
      control_send_connection_string(control_client, deviceName);
   }
}

static void control_client_disconnected(int& control_client)
{
   os_socket_close(control_client);
   control_client = -1;
}

void process_control_socket(int& control_client, overlay_params &params)
{
   if (control_client >= 0) {
      char buf[BUFSIZE];

      while (true) {
         ssize_t n = os_socket_recv(control_client, buf, BUFSIZE, 0);

         if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
               /* nothing to read, try again later */
               break;
            }

            if (errno != ECONNRESET)
               fprintf(stderr, "ERROR on connection: %s\n", strerror(errno));

            control_client_disconnected(control_client);
         } else if (n == 0) {
            /* recv() returns 0 when the client disconnects */
            control_client_disconnected(control_client);
         }

         for (ssize_t i = 0; i < n; i++) {
            process_char(control_client, params, buf[i]);
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
