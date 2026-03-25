#include <openssl/sha.h>
#include <string.h>

// Hash function from bytos to bytes
void hash(const unsigned char *input, char *output) {
  unsigned char hash[SHA512_DIGEST_LENGTH];
  SHA512((unsigned char *)input, 4096, (unsigned char *)hash);
  memcpy(output, hash, SHA512_DIGEST_LENGTH);
}
