#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

class WebSocketClient {
public:
   enum class RecvStatus {
      Message,
      Timeout,
      Closed,
      Error,
   };

   WebSocketClient() = default;
   ~WebSocketClient();

   WebSocketClient(const WebSocketClient&) = delete;
   WebSocketClient& operator=(const WebSocketClient&) = delete;

   bool connect(const std::string& host, const std::string& port,
                const std::string& path, std::chrono::milliseconds timeout);
   bool send_text(const std::string& message);
   RecvStatus recv_message(std::string& message, std::chrono::milliseconds timeout);
   void close();

   bool connected() const { return m_fd >= 0; }
   int fd() const { return m_fd; }

   static std::string websocket_accept(const std::string& key);

private:
   bool send_frame(uint8_t opcode, const std::string& payload);
   bool read_exact(void* data, size_t size, std::chrono::milliseconds timeout);
   bool wait_readable(std::chrono::milliseconds timeout);

   int m_fd = -1;
   std::string m_fragment;
   uint8_t m_fragment_opcode = 0;
};
