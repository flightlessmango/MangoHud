#include "ws_client.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <random>
#include <sstream>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace {
constexpr size_t MAX_WS_MESSAGE = 64 * 1024;
constexpr auto HEADER_TIMEOUT = std::chrono::milliseconds(5000);
constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

uint32_t rol(uint32_t value, unsigned bits)
{
   return (value << bits) | (value >> (32 - bits));
}

std::array<uint8_t, 20> sha1(const std::string& input)
{
   uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8;
   std::string msg = input;
   msg.push_back(static_cast<char>(0x80));
   while ((msg.size() % 64) != 56)
      msg.push_back(0);
   for (int i = 7; i >= 0; --i)
      msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xff));

   uint32_t h0 = 0x67452301;
   uint32_t h1 = 0xefcdab89;
   uint32_t h2 = 0x98badcfe;
   uint32_t h3 = 0x10325476;
   uint32_t h4 = 0xc3d2e1f0;

   for (size_t offset = 0; offset < msg.size(); offset += 64) {
      uint32_t w[80] {};
      for (int i = 0; i < 16; ++i) {
         size_t j = offset + i * 4;
         w[i] = (static_cast<uint8_t>(msg[j]) << 24) |
                (static_cast<uint8_t>(msg[j + 1]) << 16) |
                (static_cast<uint8_t>(msg[j + 2]) << 8) |
                static_cast<uint8_t>(msg[j + 3]);
      }
      for (int i = 16; i < 80; ++i)
         w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

      uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
      for (int i = 0; i < 80; ++i) {
         uint32_t f, k;
         if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999;
         } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
         } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
         } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
         }
         uint32_t temp = rol(a, 5) + f + e + k + w[i];
         e = d;
         d = c;
         c = rol(b, 30);
         b = a;
         a = temp;
      }

      h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
   }

   std::array<uint8_t, 20> out {};
   uint32_t h[5] {h0, h1, h2, h3, h4};
   for (int i = 0; i < 5; ++i) {
      out[i * 4] = (h[i] >> 24) & 0xff;
      out[i * 4 + 1] = (h[i] >> 16) & 0xff;
      out[i * 4 + 2] = (h[i] >> 8) & 0xff;
      out[i * 4 + 3] = h[i] & 0xff;
   }
   return out;
}

std::string base64(const uint8_t* data, size_t size)
{
   static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   std::string out;
   out.reserve(((size + 2) / 3) * 4);
   for (size_t i = 0; i < size; i += 3) {
      uint32_t v = data[i] << 16;
      if (i + 1 < size) v |= data[i + 1] << 8;
      if (i + 2 < size) v |= data[i + 2];
      out.push_back(alphabet[(v >> 18) & 0x3f]);
      out.push_back(alphabet[(v >> 12) & 0x3f]);
      out.push_back(i + 1 < size ? alphabet[(v >> 6) & 0x3f] : '=');
      out.push_back(i + 2 < size ? alphabet[v & 0x3f] : '=');
   }
   return out;
}

bool random_bytes(uint8_t* data, size_t size)
{
   int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
   if (fd >= 0) {
      size_t done = 0;
      while (done < size) {
         ssize_t ret = ::read(fd, data + done, size - done);
         if (ret <= 0) break;
         done += static_cast<size_t>(ret);
      }
      ::close(fd);
      if (done == size) return true;
   }

   std::random_device rd;
   for (size_t i = 0; i < size; ++i)
      data[i] = static_cast<uint8_t>(rd());
   return true;
}

std::string lower(std::string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
   });
   return s;
}

bool header_value(const std::string& headers, const std::string& key, std::string& value)
{
   std::istringstream in(headers);
   std::string line;
   std::string wanted = lower(key);
   while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      auto colon = line.find(':');
      if (colon == std::string::npos)
         continue;
      if (lower(line.substr(0, colon)) != wanted)
         continue;
      value = line.substr(colon + 1);
      while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
         value.erase(value.begin());
      while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
         value.pop_back();
      return true;
   }
   return false;
}

bool send_all(int fd, const char* data, size_t size)
{
   size_t done = 0;
   while (done < size) {
      ssize_t ret = ::send(fd, data + done, size - done, MSG_NOSIGNAL);
      if (ret < 0 && errno == EINTR)
         continue;
      if (ret <= 0)
         return false;
      done += static_cast<size_t>(ret);
   }
   return true;
}
}

WebSocketClient::~WebSocketClient()
{
   close();
}

std::string WebSocketClient::websocket_accept(const std::string& key)
{
   auto digest = sha1(key + WS_GUID);
   return base64(digest.data(), digest.size());
}

bool WebSocketClient::connect(const std::string& host, const std::string& port,
                              const std::string& path, std::chrono::milliseconds timeout)
{
   close();

   addrinfo hints {};
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_family = AF_UNSPEC;

   addrinfo* result = nullptr;
   if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0)
      return false;

   auto deadline = std::chrono::steady_clock::now() + timeout;
   for (addrinfo* ai = result; ai; ai = ai->ai_next) {
      int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
      if (fd < 0)
         continue;

      int flags = ::fcntl(fd, F_GETFL, 0);
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
      int ret = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
      if (ret < 0 && errno != EINPROGRESS) {
         ::close(fd);
         continue;
      }

      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining.count() < 0)
         remaining = std::chrono::milliseconds(0);
      pollfd pfd {fd, POLLOUT, 0};
      ret = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
      if (ret > 0) {
         int err = 0;
         socklen_t len = sizeof(err);
         if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
            ::fcntl(fd, F_SETFL, flags);
            m_fd = fd;
            break;
         }
      }
      ::close(fd);
   }
   ::freeaddrinfo(result);

   if (m_fd < 0)
      return false;

   std::array<uint8_t, 16> key_bytes {};
   random_bytes(key_bytes.data(), key_bytes.size());
   std::string key = base64(key_bytes.data(), key_bytes.size());
   std::string request_path = path.empty() ? "/" : path;

   std::ostringstream req;
   req << "GET " << request_path << " HTTP/1.1\r\n"
       << "Host: " << host << ":" << port << "\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Key: " << key << "\r\n"
       << "Sec-WebSocket-Version: 13\r\n\r\n";
   auto req_str = req.str();
   if (!send_all(m_fd, req_str.data(), req_str.size())) {
      close();
      return false;
   }

   std::string response;
   char c = 0;
   auto header_deadline = std::chrono::steady_clock::now() + HEADER_TIMEOUT;
   while (response.find("\r\n\r\n") == std::string::npos && response.size() < 8192) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(header_deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0 || !read_exact(&c, 1, remaining)) {
         close();
         return false;
      }
      response.push_back(c);
   }

   if (response.rfind("HTTP/1.1 101", 0) != 0 && response.rfind("HTTP/1.0 101", 0) != 0) {
      close();
      return false;
   }

   std::string accept;
   if (!header_value(response, "Sec-WebSocket-Accept", accept) || accept != websocket_accept(key)) {
      close();
      return false;
   }

   return true;
}

bool WebSocketClient::wait_readable(std::chrono::milliseconds timeout)
{
   pollfd pfd {m_fd, POLLIN, 0};
   int ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
   return ret > 0 && (pfd.revents & POLLIN);
}

bool WebSocketClient::read_exact(void* data, size_t size, std::chrono::milliseconds timeout)
{
   uint8_t* out = static_cast<uint8_t*>(data);
   size_t done = 0;
   auto deadline = std::chrono::steady_clock::now() + timeout;
   while (done < size) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0 || !wait_readable(remaining))
         return false;
      ssize_t ret = ::recv(m_fd, out + done, size - done, 0);
      if (ret <= 0)
         return false;
      done += static_cast<size_t>(ret);
   }
   return true;
}

bool WebSocketClient::send_frame(uint8_t opcode, const std::string& payload)
{
   if (m_fd < 0 || payload.size() > MAX_WS_MESSAGE)
      return false;

   std::string frame;
   frame.push_back(static_cast<char>(0x80 | opcode));
   if (payload.size() < 126) {
      frame.push_back(static_cast<char>(0x80 | payload.size()));
   } else if (payload.size() <= 0xffff) {
      frame.push_back(static_cast<char>(0x80 | 126));
      frame.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
      frame.push_back(static_cast<char>(payload.size() & 0xff));
   } else {
      frame.push_back(static_cast<char>(0x80 | 127));
      for (int i = 7; i >= 0; --i)
         frame.push_back(static_cast<char>((payload.size() >> (i * 8)) & 0xff));
   }

   uint8_t mask[4] {};
   random_bytes(mask, sizeof(mask));
   frame.append(reinterpret_cast<char*>(mask), sizeof(mask));
   for (size_t i = 0; i < payload.size(); ++i)
      frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));

   return send_all(m_fd, frame.data(), frame.size());
}

bool WebSocketClient::send_text(const std::string& message)
{
   return send_frame(0x1, message);
}

WebSocketClient::RecvStatus WebSocketClient::recv_message(std::string& message, std::chrono::milliseconds timeout)
{
   message.clear();
   if (m_fd < 0)
      return RecvStatus::Closed;

   auto deadline = std::chrono::steady_clock::now() + timeout;
   while (true) {
      uint8_t hdr[2] {};
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0)
         return RecvStatus::Timeout;
      if (!read_exact(hdr, sizeof(hdr), remaining))
         return RecvStatus::Closed;

      bool fin = hdr[0] & 0x80;
      uint8_t opcode = hdr[0] & 0x0f;
      bool masked = hdr[1] & 0x80;
      uint64_t len = hdr[1] & 0x7f;
      if (len == 126) {
         uint8_t ext[2] {};
         if (!read_exact(ext, sizeof(ext), remaining))
            return RecvStatus::Closed;
         len = (ext[0] << 8) | ext[1];
      } else if (len == 127) {
         uint8_t ext[8] {};
         if (!read_exact(ext, sizeof(ext), remaining))
            return RecvStatus::Closed;
         len = 0;
         for (uint8_t b : ext)
            len = (len << 8) | b;
      }
      if (len > MAX_WS_MESSAGE)
         return RecvStatus::Error;

      uint8_t mask[4] {};
      if (masked && !read_exact(mask, sizeof(mask), remaining))
         return RecvStatus::Closed;

      std::string payload;
      payload.resize(static_cast<size_t>(len));
      if (len && !read_exact(payload.data(), static_cast<size_t>(len), remaining))
         return RecvStatus::Closed;
      if (masked) {
         for (size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= mask[i % 4];
      }

      if (opcode == 0x8)
         return RecvStatus::Closed;
      if (opcode == 0x9) {
         send_frame(0xA, payload);
         continue;
      }
      if (opcode == 0xA)
         continue;
      if (opcode != 0x0 && opcode != 0x1)
         return RecvStatus::Error;

      if (opcode == 0x1) {
         m_fragment.clear();
         m_fragment_opcode = opcode;
      } else if (m_fragment_opcode == 0) {
         return RecvStatus::Error;
      }

      if (m_fragment.size() + payload.size() > MAX_WS_MESSAGE)
         return RecvStatus::Error;
      m_fragment += payload;
      if (fin) {
         message = m_fragment;
         m_fragment.clear();
         m_fragment_opcode = 0;
         return RecvStatus::Message;
      }
   }
}

void WebSocketClient::close()
{
   if (m_fd >= 0) {
      send_frame(0x8, "");
      ::shutdown(m_fd, SHUT_RDWR);
      ::close(m_fd);
      m_fd = -1;
   }
   m_fragment.clear();
   m_fragment_opcode = 0;
}
