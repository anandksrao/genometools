/*
** autogenerated content - DO NOT EDIT
*/
/*
  Copyright (C) 2007 Thomas Jahns <Thomas.Jahns@gmx.net>

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <time.h>
#include <sys/time.h>

#include "libgtcore/bitpackstring.h"
#include "libgtcore/env.h"
#include "libgtcore/ensure.h"

enum {
/*   MAX_RND_NUMS = 10, */
  MAX_RND_NUMS = 100000,
};

static inline int
icmp(uint32_t a, uint32_t b)
{
  if (a > b)
    return 1;
  else if (a < b)
    return -1;
  else /* if (a == b) */
    return 0;
}

/**
 * \brief bit count reference
 * @param v count the number of bits set in v
 */
static inline int
genBitCount(uint32_t v)
{
  unsigned c; /* c accumulates the total bits set in v */
  for (c = 0; v; c++)
    v &= v - 1; /* clear the least significant bit set */
  return c;
}

#define freeResourcesAndReturn(retval) \
  do {                                 \
    env_ma_free(randSrc, env);         \
    env_ma_free(randCmp, env);         \
    env_ma_free(bitStore, env);        \
    env_ma_free(bitStoreCopy, env);    \
    return retval;                     \
  } while (0)

int
bitPackStringInt32_unit_test(Env *env)
{
  BitString bitStore = NULL;
  BitString bitStoreCopy = NULL;
  uint32_t *randSrc = NULL; /*< create random ints here for input as bit
                                *  store */
  uint32_t *randCmp = NULL; /*< used for random ints read back */
  size_t i, numRnd;
  BitOffset offsetStart, offset;
  unsigned long seedval;
  int had_err = 0;
  {
    struct timeval seed;
    gettimeofday(&seed, NULL);
    seedval = seed.tv_sec + seed.tv_usec;
    srandom(seedval);
  }
  offset = offsetStart = random()%(sizeof (uint32_t) * CHAR_BIT);
  numRnd = random() % (MAX_RND_NUMS + 1);
  env_log_log(env, "seedval = %lu, offset=%lu, numRnd=%lu\n", seedval,
              (long unsigned)offsetStart, (long unsigned)numRnd);
  {
    BitOffset numBits = sizeof (uint32_t) * CHAR_BIT * numRnd + offsetStart;
    randSrc = env_ma_malloc(env, sizeof (uint32_t)*numRnd);
    bitStore = env_ma_malloc(env, bitElemsAllocSize(numBits)
                             * sizeof (BitElem));
    bitStoreCopy = env_ma_calloc(env, bitElemsAllocSize(numBits),
                                 sizeof (BitElem));
    randCmp = env_ma_malloc(env, sizeof (uint32_t)*numRnd);
  }
  /* first test unsigned types */
  env_log_log(env, "bsStoreUInt32/bsGetUInt32: ");
  for (i = 0; i < numRnd; ++i)
  {
#if 32 > 32 && LONG_BIT < 32
    uint32_t v = randSrc[i] = (uint32_t)random() << 32 | random();
#else /* 32 > 32 && LONG_BIT < 32 */
    uint32_t v = randSrc[i] = random();
#endif /* 32 > 32 && LONG_BIT < 32 */
    int bits = requiredUInt32Bits(v);
    bsStoreUInt32(bitStore, offset, bits, v);
    offset += bits;
  }
  offset = offsetStart;
  for (i = 0; i < numRnd; ++i)
  {
    uint32_t v = randSrc[i];
    int bits = requiredUInt32Bits(v);
    uint32_t r = bsGetUInt32(bitStore, offset, bits);
    ensure(had_err, r == v);
    if (had_err)
    {
      env_log_log(env, "Expected %"PRIu32", got %"PRIu32
                  ", seed = %lu, i = %lu\n",
                  v, r, seedval, (unsigned long)i);
      freeResourcesAndReturn(had_err);
    }
    offset += bits;
  }
  env_log_log(env, "passed\n");
  if (numRnd > 0)
  {
    uint32_t v = randSrc[0], r = 0;
    unsigned numBits = requiredUInt32Bits(v);
    BitOffset i = offsetStart + numBits;
    uint32_t mask = ~(uint32_t)0;
    if (numBits < 32)
      mask = ~(mask << numBits);
    env_log_log(env, "bsSetBit, bsClearBit, bsToggleBit, bsGetBit: ");
    while (v)
    {
      int lowBit = v & 1;
      v >>= 1;
      ensure(had_err, lowBit == (r = bsGetBit(bitStore, --i)));
      if (had_err)
      {
        env_log_log(env, "Expected %d, got %d, seed = %lu, i = %llu\n",
                    lowBit, (int)r, seedval, (unsigned long long)i);
        freeResourcesAndReturn(had_err);
      }
    }
    i = offsetStart + numBits;
    bsClear(bitStoreCopy, offsetStart, numBits, random()&1);
    v = randSrc[0];
    while (v)
    {
      int lowBit = v & 1;
      v >>= 1;
      if (lowBit)
        bsSetBit(bitStoreCopy, --i);
      else
        bsClearBit(bitStoreCopy, --i);
    }
    v = randSrc[0];
    r = bsGetUInt32(bitStoreCopy, offsetStart, numBits);
    ensure(had_err, r == v);
    if (had_err)
    {
      env_log_log(env, "Expected %"PRIu32", got %"PRIu32
                  ", seed = %lu\n", v, r, seedval);
      freeResourcesAndReturn(had_err);
    }
    for (i = 0; i < numBits; ++i)
      bsToggleBit(bitStoreCopy, offsetStart + i);
    r = bsGetUInt32(bitStoreCopy, offsetStart, numBits);
    ensure(had_err, r == (v = (~v & mask)));
    if (had_err)
    {
      env_log_log(env, "Expected %"PRIu32", got %"PRIu32
                  ", seed = %lu\n", v, r, seedval);
      freeResourcesAndReturn(had_err);
    }
    env_log_log(env, "passed\n");
  }
  if (numRnd > 1)
  {
    env_log_log(env, "bsCompare: ");
    {
      uint32_t v0 = randSrc[0];
      int bits0 = requiredUInt32Bits(v0);
      uint32_t r0;
      offset = offsetStart;
      r0 = bsGetUInt32(bitStore, offset, bits0);
      for (i = 1; i < numRnd; ++i)
      {
        uint32_t v1 = randSrc[i];
        int bits1 = requiredUInt32Bits(v1);
        uint32_t r1 = bsGetUInt32(bitStore, offset + bits0, bits1);
        int result = -2;   /*< -2 is not a return value of bsCompare, thus
                            *   if it is displayed, there was an earlier
                            *   error. */
        ensure(had_err, r0 == v0 && r1 == v1);
        ensure(had_err, icmp(v0, v1) ==
               (result = bsCompare(bitStore, offset, bits0,
                                   bitStore, offset + bits0, bits1)));
        if (had_err)
        {
          env_log_log(env, "Expected v0 %s v1, got v0 %s v1,\n for v0=%"
                      PRIu32" and v1=%"PRIu32",\n"
                      "seed = %lu, i = %lu, bits0=%u, bits1=%u\n",
                      (v0 > v1?">":(v0 < v1?"<":"==")),
                      (result > 0?">":(result < 0?"<":"==")), v0, v1,
                      seedval, (unsigned long)i, bits0, bits1);
          freeResourcesAndReturn(had_err);
        }
        offset += bits0;
        bits0 = bits1;
        v0 = v1;
        r0 = r1;
      }
    }
    env_log_log(env, "passed\n");
  }
  env_log_log(env, "bsStoreUniformUInt32Array/bsGetUInt32: ");
  {
    unsigned numBits = random()%32 + 1;
    uint32_t mask = ~(uint32_t)0;
    if (numBits < 32)
      mask = ~(mask << numBits);
    offset = offsetStart;
    bsStoreUniformUInt32Array(bitStore, offset, numBits, numRnd, randSrc);
    for (i = 0; i < numRnd; ++i)
    {
      uint32_t v = randSrc[i] & mask;
      uint32_t r = bsGetUInt32(bitStore, offset, numBits);
      ensure(had_err, r == v);
      if (had_err)
      {
        env_log_log(env, "Expected %"PRIu32", got %"PRIu32",\n"
                    "seed = %lu, i = %lu, bits=%u\n",
                    v, r, seedval, (unsigned long)i, numBits);
        freeResourcesAndReturn(had_err);
      }
      offset += numBits;
    }
    env_log_log(env, "passed\n");
    env_log_log(env,
                "bsStoreUniformUInt32Array/bsGetUniformUInt32Array: ");
    bsGetUniformUInt32Array(bitStore, offset = offsetStart,
                               numBits, numRnd, randCmp);
    for (i = 0; i < numRnd; ++i)
    {
      uint32_t v = randSrc[i] & mask;
      uint32_t r = randCmp[i];
      ensure(had_err, r == v);
      if (had_err)
      {
        env_log_log(env,
                    "Expected %"PRIu32", got %"PRIu32",\n seed = %lu,"
                    " i = %lu, bits=%u\n",
                    v, r, seedval, (unsigned long)i, numBits);
        freeResourcesAndReturn(had_err);
      }
    }
    if (numRnd > 1)
    {
      uint32_t v = randSrc[0] & mask;
      uint32_t r;
      bsGetUniformUInt32Array(bitStore, offsetStart,
                                 numBits, 1, &r);
      if (r != v)
      {
        env_log_log(env,
                    "Expected %"PRIu32", got %"PRIu32", seed = %lu,"
                    " one value extraction\n",
                    v, r, seedval);
        freeResourcesAndReturn(had_err);
      }
    }
    env_log_log(env, " passed\n");
  }
  /* int types */
  env_log_log(env, "bsStoreInt32/bsGetInt32: ");
  for (i = 0; i < numRnd; ++i)
  {
    int32_t v = (int32_t)randSrc[i];
    unsigned bits = requiredInt32Bits(v);
    bsStoreInt32(bitStore, offset, bits, v);
    offset += bits;
  }
  offset = offsetStart;
  for (i = 0; i < numRnd; ++i)
  {
    int32_t v = randSrc[i];
    unsigned bits = requiredInt32Bits(v);
    int32_t r = bsGetInt32(bitStore, offset, bits);
    ensure(had_err, r == v);
    if (had_err)
    {
      env_log_log(env, "Expected %"PRId32", got %"PRId32",\n"
                  "seed = %lu, i = %lu, bits=%u\n",
                  v, r, seedval, (unsigned long)i, bits);
      freeResourcesAndReturn(had_err);
    }
    offset += bits;
  }
  env_log_log(env, "passed\n");
  env_log_log(env, "bsStoreUniformInt32Array/bsGetInt32: ");
  {
    unsigned numBits = random()%32 + 1;
    int32_t mask = ~(int32_t)0;
    if (numBits < 32)
      mask = ~(mask << numBits);
    offset = offsetStart;
    bsStoreUniformInt32Array(bitStore, offset, numBits, numRnd,
                                (int32_t *)randSrc);
    for (i = 0; i < numRnd; ++i)
    {
      int32_t m = (int32_t)1 << (numBits - 1);
      int32_t v = (int32_t)((randSrc[i] & mask) ^ m) - m;
      int32_t r = bsGetInt32(bitStore, offset, numBits);
      ensure(had_err, r == v);
      if (had_err)
      {
        env_log_log(env, "Expected %"PRId32", got %"PRId32",\n"
                    "seed = %lu, i = %lu, numBits=%u\n",
                    v, r, seedval, (unsigned long)i, numBits);
        freeResourcesAndReturn(had_err);
      }
      offset += numBits;
    }
    env_log_log(env, "passed\n");
    env_log_log(env, "bsStoreUniformInt32Array/bsGetUniformInt32Array: ");
    bsGetUniformInt32Array(bitStore, offset = offsetStart,
                              numBits, numRnd, (int32_t *)randCmp);
    for (i = 0; i < numRnd; ++i)
    {
      int32_t m = (int32_t)1 << (numBits - 1);
      int32_t v = (int32_t)((randSrc[i] & mask) ^ m) - m;
      int32_t r = randCmp[i];
      ensure(had_err, r == v);
      if (had_err)
      {
        env_log_log(env, "Expected %"PRId32", got %"PRId32
                    ", seed = %lu, i = %lu\n",
                    v, r, seedval, (unsigned long)i);
        freeResourcesAndReturn(had_err);
      }
    }
    if (numRnd > 0)
    {
      int32_t m = (int32_t)1 << (numBits - 1);
      int32_t v = (int32_t)((randSrc[0] & mask) ^ m) - m;
      int32_t r = 0;
      bsGetUniformInt32Array(bitStore, offsetStart,
                                numBits, 1, &r);
      ensure(had_err, r == v);
      if (had_err)
      {
        env_log_log(env, "Expected %"PRId32", got %"PRId32
                    ", seed = %lu, one value extraction\n",
                    v, r, seedval);
        freeResourcesAndReturn(had_err);
      }
    }
    env_log_log(env, "passed\n");
  }
  if (numRnd > 0)
  {
    env_log_log(env, "bsCopy: ");
    {
      /* first decide how many of the values to use and at which to start */
      size_t numValueCopies, copyStart;
      BitOffset numCopyBits = 0, destOffset;
      unsigned numBits = random()%32 + 1;
      uint32_t mask = ~(uint32_t)0;
      if (numBits < 32)
        mask = ~(mask << numBits);
      if (random()&1)
      {
        numValueCopies = random()%(numRnd + 1);
        copyStart = random()%(numRnd - numValueCopies + 1);
      }
      else
      {
        copyStart = random() % numRnd;
        numValueCopies = random()%(numRnd - copyStart) + 1;
      }
      assert(copyStart + numValueCopies <= numRnd);
      offset = offsetStart + (BitOffset)copyStart * numBits;
      bsStoreUniformUInt32Array(bitStore, offset, numBits, numValueCopies,
                                    randSrc);
      destOffset = random()%(offsetStart + 32
                             * (BitOffset)(numRnd - numValueCopies) + 1);
      numCopyBits = (BitOffset)numBits * numValueCopies;
      /* the following bsCopy should be equivalent to:
       * bsStoreUniformUInt32Array(bitStoreCopy, destOffset,
       *                              numBits, numValueCopies, randSrc); */
      bsCopy(bitStore, offset, bitStoreCopy, destOffset, numCopyBits);
      ensure(had_err,
             bsCompare(bitStore, offset, numCopyBits,
                       bitStoreCopy, destOffset, numCopyBits) == 0);
      if (had_err)
      {
        env_log_log(env, "Expected equality on bitstrings\n"
                    "seed = %lu, offset = %llu, destOffset = %llu,"
                    " numCopyBits=%llu\n",
                    seedval, (unsigned long long)offset,
                    (unsigned long long)destOffset,
                    (unsigned long long)numCopyBits);
        /* FIXME: implement bitstring output function */
        freeResourcesAndReturn(had_err);
      }
      env_log_log(env, "passed\n");
    }
  }
  if (numRnd > 0)
  {
    env_log_log(env, "bsClear: ");
    {
      /* first decide how many of the values to use and at which to start */
      size_t numResetValues, resetStart;
      BitOffset numResetBits = 0;
      unsigned numBits = random()%32 + 1;
      int bitVal = random()&1;
      int32_t cmpVal = bitVal?-1:0;
      uint32_t mask = ~(uint32_t)0;
      if (numBits < 32)
        mask = ~(mask << numBits);
      if (random()&1)
      {
        numResetValues = random()%(numRnd + 1);
        resetStart = random()%(numRnd - numResetValues + 1);
      }
      else
      {
        resetStart = random() % numRnd;
        numResetValues = random()%(numRnd - resetStart) + 1;
      }
      assert(resetStart + numResetValues <= numRnd);
      offset = offsetStart;
      bsStoreUniformInt32Array(bitStore, offset, numBits, numRnd,
                                    (int32_t *)randSrc);
      numResetBits = (BitOffset)numBits * numResetValues;
      bsClear(bitStore, offset + (BitOffset)resetStart * numBits,
              numResetBits, bitVal);
      {
        int32_t m = (int32_t)1 << (numBits - 1);
        for (i = 0; i < resetStart; ++i)
        {
          int32_t v = (int32_t)((randSrc[i] & mask) ^ m) - m;
          int32_t r = bsGetInt32(bitStore, offset, numBits);
          ensure(had_err, r == v);
          if (had_err)
          {
            env_log_log(env, "Expected %"PRId32", got %"PRId32",\n"
                        "seed = %lu, i = %lu, numBits=%u\n",
                        v, r, seedval, (unsigned long)i, numBits);
            freeResourcesAndReturn(had_err);
          }
          offset += numBits;
        }
        for (; i < resetStart + numResetValues; ++i)
        {
          int32_t r = bsGetInt32(bitStore, offset, numBits);
          ensure(had_err, r == cmpVal);
          if (had_err)
          {
            env_log_log(env, "Expected %"PRId32", got %"PRId32",\n"
                        "seed = %lu, i = %lu, numBits=%u\n",
                        cmpVal, r, seedval, (unsigned long)i, numBits);
            freeResourcesAndReturn(had_err);
          }
          offset += numBits;
        }
        for (; i < numRnd; ++i)
        {
          int32_t v = (int32_t)((randSrc[i] & mask) ^ m) - m;
          int32_t r = bsGetInt32(bitStore, offset, numBits);
          ensure(had_err, r == v);
          if (had_err)
          {
            env_log_log(env, "Expected %"PRId32", got %"PRId32",\n"
                        "seed = %lu, i = %lu, numBits=%u\n",
                        v, r, seedval, (unsigned long)i, numBits);
            freeResourcesAndReturn(had_err);
          }
          offset += numBits;
        }
      }
    }
    env_log_log(env, "passed\n");
  }
  if (numRnd > 0)
  {
    env_log_log(env, "bs1BitsCount: ");
    {
      /* first decide how many of the values to use and at which to start */
      size_t numCountValues, countStart;
      BitOffset numCountBits = 0, bitCountRef = 0, bitCountCmp;
      unsigned numBits = random()%32 + 1;
      uint32_t mask = ~(uint32_t)0;
      if (numBits < 32)
        mask = ~(mask << numBits);
      if (random()&1)
      {
        numCountValues = random()%(numRnd + 1);
        countStart = random()%(numRnd - numCountValues + 1);
      }
      else
      {
        countStart = random() % numRnd;
        numCountValues = random()%(numRnd - countStart) + 1;
      }
      assert(countStart + numCountValues <= numRnd);
      offset = offsetStart;
      bsStoreUniformUInt32Array(bitStore, offset, numBits, numRnd, randSrc);
      numCountBits = (BitOffset)numBits * numCountValues;
      bitCountCmp = bs1BitsCount(bitStore,
                                 offset + (BitOffset)countStart * numBits,
                                 numCountBits);
      for (i = countStart; i < countStart + numCountValues; ++i)
      {
        uint32_t v = (uint32_t)randSrc[i] & mask;
        bitCountRef += genBitCount(v);
      }
      ensure(had_err, bitCountRef == bitCountCmp);
      if (had_err)
      {
        env_log_log(env, "Expected %llu, got %llu,\n"
                    "seed = %lu, numBits=%u\n", (unsigned long long)bitCountRef,
                    (unsigned long long)bitCountCmp, seedval, numBits);
        freeResourcesAndReturn(had_err);
      }
      offset += numBits;
    }
    env_log_log(env, "passed\n");
  }
  freeResourcesAndReturn(had_err);
}
