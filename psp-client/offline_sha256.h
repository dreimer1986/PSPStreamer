/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <mbedtls/sha256.h>
/* File verification only. Does not replace Mbed TLS's TLS/hash entry points. */
int offline_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *data,size_t size);
