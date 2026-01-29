/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sha256.h
  * @brief   SHA256 hash calculation
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __SHA256_H
#define __SHA256_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stddef.h>

/* Exported constants --------------------------------------------------------*/
#define SHA256_BLOCK_SIZE            64
#define SHA256_DIGEST_SIZE           32
#define SHA256_DIGEST_HEX_LEN        64

/* Exported types ------------------------------------------------------------*/
typedef struct {
    uint8_t data[SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx_t;

/* Exported functions prototypes ---------------------------------------------*/
void SHA256_Init(sha256_ctx_t* ctx);
void SHA256_Update(sha256_ctx_t* ctx, const uint8_t* data, size_t len);
void SHA256_Final(sha256_ctx_t* ctx, uint8_t* hash);
void SHA256_Calculate(const uint8_t* data, size_t len, uint8_t* hash);
void SHA256_HashToHex(const uint8_t* hash, char* hex_string);

#ifdef __cplusplus
}
#endif

#endif /* __SHA256_H */
