// SHA256 implementation - header only
// Based on public domain implementation
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace rankd {
namespace sha256 {

inline std::string hash(const std::string &input) {
  // SHA256 constants
  static constexpr std::array<uint32_t, 64> K = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  auto rotr = [](uint32_t x, uint32_t n) -> uint32_t {
    return (x >> n) | (x << (32 - n));
  };

  // Initial hash values
  uint32_t h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a,
           h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

  // Pre-processing: adding padding bits
  std::string msg = input;
  uint64_t original_bit_len = msg.size() * 8;
  msg += static_cast<char>(0x80);
  while ((msg.size() % 64) != 56) {
    msg += static_cast<char>(0x00);
  }

  // Append original length in bits as 64-bit big-endian
  for (int i = 7; i >= 0; --i) {
    msg += static_cast<char>((original_bit_len >> (i * 8)) & 0xff);
  }

  // Process each 64-byte chunk
  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    std::array<uint32_t, 64> w{};

    // Break chunk into 16 32-bit big-endian words
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(msg[chunk + i * 4]))
              << 24) |
             (static_cast<uint32_t>(static_cast<uint8_t>(msg[chunk + i * 4 + 1]))
              << 16) |
             (static_cast<uint32_t>(static_cast<uint8_t>(msg[chunk + i * 4 + 2]))
              << 8) |
             static_cast<uint32_t>(static_cast<uint8_t>(msg[chunk + i * 4 + 3]));
    }

    // Extend the 16 words into 64 words
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    // Working variables
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;

    // Compression function
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + S1 + ch + K[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
    h5 += f;
    h6 += g;
    h7 += h;
  }

  // Produce the final hash value (big-endian)
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  result << std::setw(8) << h0 << std::setw(8) << h1 << std::setw(8) << h2
         << std::setw(8) << h3 << std::setw(8) << h4 << std::setw(8) << h5
         << std::setw(8) << h6 << std::setw(8) << h7;
  return result.str();
}

} // namespace sha256
} // namespace rankd
