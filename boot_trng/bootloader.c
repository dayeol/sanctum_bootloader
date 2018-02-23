
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#define ED25519_NO_SEED 1
#include "ed25519/ed25519.h"
/* Adopted from https://github.com/orlp/ed25519
  provides:
  - void ed25519_create_keypair(t_pubkey *public_key, t_privkey *private_key, t_seed *seed);
  - void ed25519_sign(t_signature *signature,
                      const unsigned uint8_t *message,
                      size_t message_len,
                      t_pubkey *public_key,
                      t_privkey *private_key);
*/

#include "ed25519/sha512.h"
/* adopted from
  provides:
  - int sha512_init(sha512_context * md);
  - int sha512_update(sha512_context * md, const unsigned char *in, size_t inlen);
  - int sha512_final(sha512_context * md, unsigned char *out);
  types: sha512_context
*/

#include "string.h"
/*
  provides memcpy, memset
*/

#include "csr.h"
/*
  provides read_csr
*/

typedef unsigned char byte;

// Sanctum header fields in DRAM
extern byte sanctum_dev_public_key[32];
extern byte sanctum_dev_secret_key[64];
extern unsigned int * sanctum_sm_size;
extern byte sanctum_sm_hash[64];
extern byte sanctum_sm_public_key[32];
extern byte sanctum_sm_secret_key[64];
extern byte sanctum_sm_signature[64];
extern const byte * sanctum_sm_ptr;

inline byte random_byte() {
  return (byte)(read_csr(trng));
}

void bootloader() {
  // Reserve stack space for secrets
  byte scratchpad[64];
  sha512_context hash_ctx;

  // TODO: on real device, copy boot image from memory. In simulator, HTIF writes boot image
  // ... SD card to beginning of memory.
  // sd_init();
  // sd_read_from_start(DRAM, 1024);

  // Create a random seed for a device key from TRNG
  for (unsigned int i=0; i<32; i++) {
    scratchpad[i] =random_byte();
  }

  // Derive {SK_D, PK_D} (device keys) from a 32 B random seed
  ed25519_create_keypair(sanctum_dev_public_key, sanctum_dev_secret_key, scratchpad);

  // Measure SM
  sha512_init(&hash_ctx);

  /*
  asm volatile (
    "la t0, sanctum_sm_ptr \n"
    "la t1, sanctum_sm_size \n"
    "lw t2, sanctum_sm_size \n"
    "ld t2, -1(x0)"
    : : :
  );
  */

  //const byte * _sanctum_sm_ptr = (const unsigned char *)0x8000028c;
  //unsigned int * _sanctum_sm_size = (unsigned int *)0x80000168;
  //*sanctum_sm_size = 0x1ffd704;

  //sha512_update(&hash_ctx, sanctum_sm_ptr, *sanctum_sm_size);
  //sha512_update(&hash_ctx, _sanctum_sm_ptr, *_sanctum_sm_size);
  sha512_update(&hash_ctx, (const unsigned char *)0x8000028c, *((unsigned int *)0x80000168));
  sha512_final(&hash_ctx, sanctum_sm_hash);

  // Combine SK_D and H_SM via a hash
  // sm_key_seed <-- H(SK_D, H_SM), truncate to 32B
  sha512_init(&hash_ctx);
  sha512_update(&hash_ctx, sanctum_dev_secret_key, sizeof(*sanctum_dev_secret_key));
  sha512_update(&hash_ctx, sanctum_sm_hash, sizeof(*sanctum_sm_hash));
  sha512_final(&hash_ctx, scratchpad);
  // Derive {SK_D, PK_D} (device keys) from the first 32 B of the hash (NIST endorses SHA512 truncation as safe)
  ed25519_create_keypair(sanctum_sm_public_key, sanctum_sm_secret_key, scratchpad);

  // Endorse the SM
  // Comute H(H_SM, PK_SM)
  sha512_init(&hash_ctx);
  sha512_update(&hash_ctx, sanctum_sm_hash, sizeof(*sanctum_sm_hash));
  sha512_update(&hash_ctx, sanctum_sm_public_key, sizeof(*sanctum_sm_public_key));
  sha512_final(&hash_ctx, scratchpad);
  // Sign H(H_SM, PK_SM) with SK_D
  ed25519_sign(sanctum_sm_signature, scratchpad, sizeof(scratchpad), sanctum_dev_public_key, sanctum_dev_secret_key);

  // Clean up
  // Erase SK_D
  memset((void*)sanctum_dev_secret_key, 0, sizeof(*sanctum_sm_secret_key));

  // caller will clean core state and memory (including the stack), and boot.
  return;
}
