#include "ws_client.h"

#include <cassert>

int main()
{
   assert(WebSocketClient::websocket_accept("dGhlIHNhbXBsZSBub25jZQ==") ==
          "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
   return 0;
}
