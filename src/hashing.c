#include <openssl/sha.h>
#include <string.h>

// Hash function from bytes to bytes
void hash(const unsigned char *input, unsigned char *output) {
  unsigned char h[SHA512_DIGEST_LENGTH];
  SHA512(input, 4096, h);
  memcpy(output, h, SHA512_DIGEST_LENGTH);
}
