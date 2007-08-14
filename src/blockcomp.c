/*
** Copyright (C) 2007 Thomas Jahns <Thomas.Jahns@gmx.net>
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**  
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**  
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**  
*/
/**
 * \file blockcomp.c
 * \brief Methods to build block-compressed representation of indexed
 * sequence.
 * \author Thomas Jahns <Thomas.Jahns@gmx.net>
 */
/*
  TODO:
  - normalize use  of  seqIdx variable naming (seq, bseq etc.)
  - distribute init/new functionality cleanly
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifndef DEBUG
#define NDEBUG
#endif /* DEBUG */
#include <assert.h>
#include <stddef.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif /* HAVE_INTTYPES_H */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */
#include <string.h>
#ifndef NDEBUG
#include <math.h>
#endif

#include <libgtcore/bitpackstring.h>
#include <libgtcore/env.h>
#include <libgtcore/minmax.h>
#include <libgtcore/str.h>
#include <libgtmatch/sarr-def.h>
#include <libgtmatch/sfx-map.pr>
#include <libgtmatch/chardef.h>

#include "biofmi2.h"
#include "biofmi2misc.h"
#include "encidxseq.h"
#include "encidxseqpriv.h"
#include "seqranges.h"

struct onDiskBlockCompIdx 
{
  FILE *IdxData;
  off_t cwIdxDataPos,              /*< constant width data of the index */
    varIdxDataPos,                      /*< variable width part */
    rangeEncPos;                   /*< in-file position of special
                                    *  symbol representation */
};

static int
initOnDiskBlockCompIdx(struct onDiskBlockCompIdx *idx,
                       Str *projectName,  off_t cwLen, Env *env);
static void
destructOnDiskBlockCompIdx(struct onDiskBlockCompIdx *idx, Env *env);

struct blockCompositionSeq
{
  struct encIdxSeq baseClass;
  unsigned blockSize;
  size_t superBucketLen;
  struct seqRangeList *rangeEncs;
  Symbol blockEncNumSyms, fallback;
  struct compList *compositionTable;
  struct onDiskBlockCompIdx externalData;
  MRAEnc *alphabet, *blockMapAlphabet;
  int *modes;
};

static const struct encIdxSeqClass blockCompositionSeqClass;

static inline struct blockCompositionSeq *
encIdxSeq2blockCompositionSeq(struct encIdxSeq *seq)
{
  assert(seq && seq->classInfo == &blockCompositionSeqClass);
  return (struct blockCompositionSeq *)((char *)seq
    - offsetof(struct blockCompositionSeq, baseClass));
}

static inline struct encIdxSeq *
blockCompositionSeq2encIdxSeq(struct blockCompositionSeq *seq)
{
  assert(seq);
  return &(seq->baseClass);
}

static inline const struct blockCompositionSeq *
constEncIdxSeq2blockCompositionSeq(const struct encIdxSeq *seq)
{
  assert(seq && seq->classInfo == &blockCompositionSeqClass);
  return (struct blockCompositionSeq *)((char *)seq
    - offsetof(struct blockCompositionSeq, baseClass));
}

static inline const struct encIdxSeq *
constBlockCompositionSeq2encIdxSeq(const struct blockCompositionSeq *seq)
{
  assert(seq);
  return &(seq->baseClass);
}

struct permList
{
  size_t numPermutations;
  unsigned permIdxBits;
  BitString catPerms;
};

struct compList
{
  size_t numCompositions;
  struct permList *permutations;
  BitString catComps;                  /*< bitstring with all
                                        * compositions encoded in
                                        * minimal space concatenated
                                        * together */
  unsigned bitsPerCount, bitsPerSymbol, compositionIdxBits, maxPermIdxBits;
  size_t maxPermCount;
};

static struct compList *
newCompositionList(const struct blockCompositionSeq *seqIdx,
                   unsigned numSyms, Env *env);
static void
deleteCompositionList(struct compList *clist, Env *env);

static int
block2IndexPair(const struct blockCompositionSeq *seqIdx,
                const Symbol *block,
                size_t idxOutput[2],
                unsigned *bitsOfPermIdx,
                Env *env,
                BitString permCompPA, Symbol *compPA);

static unsigned
symCountFromComposition(const struct blockCompositionSeq *seqIdx,
                        size_t compIndex, Symbol sym);

struct appendState
{
  BitString compCache, permCache;
  BitOffset compCacheLen, permCacheLen;
  BitOffset cwMemPos, varMemPos, varDiskOffset, varMemOldBits;
  off_t cwDiskOffset;
};

static void
initAppendState(struct appendState *aState,
                BitOffset compCacheLen, BitOffset permCacheLen,
                Env *env);
static void
destructAppendState(struct appendState *aState, Env *env);

static int
append2IdxOutput(struct blockCompositionSeq *newSeqIdx,
                 struct appendState *state,
                 size_t permCompIdx[2],
                 unsigned bitsOfCompositionIdx, unsigned bitsOfPermutationIdx,
                 Env *env);

typedef Seqpos *superBucket;

static inline superBucket
newSuperBucket(size_t alphabetSize, Env *env);

static inline void
deleteSuperBucket(superBucket supB, Env *env);

static void
addBlock2SuperBucket(superBucket supB, Symbol *block, size_t blockSize);

static inline size_t
superBucketSize(unsigned compositionIdxBits, unsigned alphabetSize,
                size_t superBucketLen, unsigned blockSize);

static inline off_t
cwSize(Seqpos seqLen, unsigned compositionIdxBits,
       unsigned alphabetSize, size_t superBucketLen, unsigned blockSize);

static void
addRangeEncodedSyms(struct seqRangeList *rangeList, Symbol *block,
                    size_t blockSize,
                    Seqpos blockNum, MRAEnc *alphabet,
                    int selection, int *rangeSel, Env *env);

static int
updateIdxOutput(struct blockCompositionSeq *seqIdx,
                struct appendState *aState,
                superBucket buck);

static int
finalizeIdxOutput(struct blockCompositionSeq *seqIdx,
                  struct appendState *state,
                  superBucket buck, Env *env);

#define newBlockEncIdxSeqErrRet()                                       \
  do {                                                                  \
    if(newSeqIdx->externalData.IdxData)                                 \
      destructOnDiskBlockCompIdx(&newSeqIdx->externalData, env);        \
    if(newSeqIdx->compositionTable)                                     \
      deleteCompositionList(newSeqIdx->compositionTable, env);          \
    if(newSeqIdx->rangeEncs)                                            \
      deleteSeqRangeList(newSeqIdx->rangeEncs, env);                    \
    if(newSeqIdx) env_ma_free(newSeqIdx, env);                          \
    freesuffixarray(&suffixArray,env);                                  \
    if(alphabet) env_ma_free(alphabet, env);                            \
    if(modesCopy)                                                       \
      env_ma_free(modesCopy, env);                                      \
    if(blockMapAlphabet) env_ma_free(blockMapAlphabet, env);            \
    return NULL;                                                        \
  } while(0)

struct encIdxSeq *
newBlockEncIdxSeq(enum rangeStoreMode modes[],
                  Str *projectName, unsigned blockSize,
                  Env *env)
{
  struct blockCompositionSeq *newSeqIdx = NULL;
  Symbol blockMapAlphabetSize, totalAlphabetSize;
  Suffixarray suffixArray;
  MRAEnc *alphabet = NULL, *blockMapAlphabet = NULL;
  BitOffset bitsPerComposition, bitsPerPermutation;
  unsigned compositionIdxBits;
  size_t superBucketLen[] = { blockSize * blockSize };
  Seqpos seqLen;
  int *modesCopy = NULL;
  assert(blockSize > 0);
  assert(modes && projectName);
  env_error_check(env);
  newSeqIdx = env_ma_calloc(env, sizeof(struct blockCompositionSeq), 1);
  newSeqIdx->superBucketLen = superBucketLen[0];
  /* map and interpret index project file */
  if(streamsuffixarray(&suffixArray, &newSeqIdx->baseClass.seqLen,
                       SARR_SUFTAB | SARR_BWTTAB, projectName, true, env))
    return NULL;
  seqLen = ++(newSeqIdx->baseClass.seqLen);
  /* convert alphabet */
  newSeqIdx->alphabet = alphabet = MRAEncGTAlphaNew(suffixArray.alpha, env);
  MRAEncAddSymbolToRange(alphabet, SEPARATOR, 1);
  /* TODO: improve guessing of number of necessary ranges */
  newSeqIdx->rangeEncs = newSeqRangeList(seqLen / 100, env);
  {
    size_t range, numAlphabetRanges = MRAEncGetNumRanges(alphabet);
    totalAlphabetSize = MRAEncGetSize(alphabet);
    blockMapAlphabetSize = 0;
    newSeqIdx->modes = modesCopy =
      env_ma_malloc(env, sizeof(int) * numAlphabetRanges);
    for(range = 0; range < numAlphabetRanges; ++range)
    {
      modesCopy[range] = modes[range];
      switch(modes[range])
      {
      case BLOCK_COMPOSITION_INCLUDE:
        blockMapAlphabetSize += MRAEncGetRangeSize(alphabet, range);
        break;
      case DIRECT_SYM_ENCODE:
        /*< FIXME: insert proper code to process ranges */
        break;
      case REGIONS_LIST:
        /*< FIXME: insert proper code to process ranges */
        break;
      default:
        /* TODO: improve diagnostics */
        fprintf(stderr, "Invalid encoding request.\n");
        newBlockEncIdxSeqErrRet();
        break;
      }
    }
    newSeqIdx->blockMapAlphabet = blockMapAlphabet =
      MRAEncSecondaryMapping(alphabet, BLOCK_COMPOSITION_INCLUDE,
                             modesCopy, newSeqIdx->fallback = 0, env);
    assert(MRAEncGetSize(blockMapAlphabet) == blockMapAlphabetSize);
  }
  /* find wether the sequence length is known */
  if(!suffixArray.longest.defined)
    newBlockEncIdxSeqErrRet();
  newSeqIdx->baseClass.classInfo = &blockCompositionSeqClass;
  newSeqIdx->blockSize = blockSize;
  newSeqIdx->blockEncNumSyms = blockMapAlphabetSize;
  if(!(newSeqIdx->compositionTable =
       newCompositionList(newSeqIdx, blockMapAlphabetSize, env)))
    newBlockEncIdxSeqErrRet();
  bitsPerComposition = newSeqIdx->compositionTable->bitsPerCount
    * blockMapAlphabetSize;
  compositionIdxBits = newSeqIdx->compositionTable->compositionIdxBits;
  bitsPerPermutation = newSeqIdx->compositionTable->bitsPerSymbol * blockSize;
  initOnDiskBlockCompIdx(&newSeqIdx->externalData, projectName,
                         cwSize(seqLen, compositionIdxBits, 
                                newSeqIdx->blockEncNumSyms,
                                superBucketLen[0], blockSize), env);
  /* At this point everything should be ready to receive the actual sequence
   * information, steps: */
  {
    FILE *bwtFP = NULL, *sufTabFP = NULL;
    int hadError = 0;
    do {
      /* 1. get bwttab file pointer */
      bwtFP = suffixArray.bwttabstream.fp;
      /* 2. get suffix array file pointer */
      sufTabFP = suffixArray.suftabstream.fp;
      {
        Symbol *block;
        unsigned *compositionPreAlloc;
        BitString permCompBSPreAlloc;
        superBucket buck;
        block = env_ma_malloc(env, sizeof(Symbol) * blockSize);
        compositionPreAlloc = env_ma_malloc(env, sizeof(unsigned)
                                            * blockMapAlphabetSize);
        permCompBSPreAlloc =
          env_ma_malloc(env, bitElemsAllocSize(bitsPerComposition
                                               + bitsPerPermutation)
                        * sizeof(BitElem));
        buck = newSuperBucket(totalAlphabetSize, env);
        /* 3. read block sized chunks from bwttab and suffix array */
        {
          Seqpos numFullBlocks = seqLen / blockSize, blockNum,
            superBucketBlocks = superBucketLen[0]/blockSize;
          /* pos == seqLen - symbolsLeft */
          struct appendState aState;
          initAppendState(&aState,
                          superBucketBlocks * compositionIdxBits,
                          superBucketBlocks * 
                          newSeqIdx->compositionTable->maxPermIdxBits,
                          env);
          blockNum = 0;
          while(blockNum < numFullBlocks)
          {
            size_t permCompIdx[2];
            unsigned significantPermIdxBits;
            int readResult;
            /* 4. for each chunk: */
            readResult = MRAEncReadAndTransform(alphabet, bwtFP,
                                                blockSize, block);
            if(readResult < 0)
            {
              hadError = 1;
              perror("error condition while reading index data");
              break;
            }
            /* 4.a. update superbucket table */
            addBlock2SuperBucket(buck, block, blockSize);
            /* 4.b. add ranges of differently encoded symbols to
             * corresponding representation */
            addRangeEncodedSyms(newSeqIdx->rangeEncs, block, blockSize,
                                blockNum, alphabet, REGIONS_LIST,
                                modesCopy, env);
            /* 4.c. add to table of composition/permutation indices */
            MRAEncSymbolsTransform(blockMapAlphabet, block, blockSize);
            /* FIXME control remapping */
            block2IndexPair(newSeqIdx, block, permCompIdx,
                            &significantPermIdxBits, env, permCompBSPreAlloc,
                            compositionPreAlloc);
            append2IdxOutput(newSeqIdx, &aState,
                             permCompIdx, compositionIdxBits,
                             significantPermIdxBits, env);
            /* update on-disk structure */
            if(!((++blockNum) % superBucketBlocks))
              if(!updateIdxOutput(newSeqIdx, &aState, buck))
              {
                hadError = 1;
                perror("error condition while writing block-compressed"
                       " index data");
                break;
              }
          }
          /* handle last chunk */
          if(!hadError)
          {
            size_t permCompIdx[2];
            unsigned significantPermIdxBits;
            int readResult;
            Seqpos symbolsLeft = seqLen % blockSize;
            readResult = MRAEncReadAndTransform(alphabet, bwtFP,
                                                symbolsLeft, block);
            if(readResult < 0)
            {
              hadError = 1;
              perror("error condition while reading index data");
              break;
            }
            else
            {
              memset(block + symbolsLeft, 0,
                     sizeof(Symbol) * (blockSize - symbolsLeft));
              addBlock2SuperBucket(buck, block, blockSize);
              addRangeEncodedSyms(newSeqIdx->rangeEncs, block, blockSize,
                                  blockNum, alphabet, REGIONS_LIST,
                                  modesCopy, env);
              MRAEncSymbolsTransform(blockMapAlphabet, block, blockSize);
              block2IndexPair(newSeqIdx, block, permCompIdx,
                              &significantPermIdxBits, env, permCompBSPreAlloc,
                              compositionPreAlloc);
              append2IdxOutput(newSeqIdx, &aState,
                               permCompIdx, compositionIdxBits,
                               significantPermIdxBits, env);
              if(!updateIdxOutput(newSeqIdx, &aState, buck))
              {
                hadError = 1;
                perror("error condition while writing block-compressed"
                       " index data");
                break;
              }
              if(!finalizeIdxOutput(newSeqIdx, &aState, buck, env))
              {
                hadError = 1;
                perror("error condition while writing block-compressed"
                       " index data");
                break;
              }
            }
          }
          if(hadError)
          {
            destructAppendState(&aState, env);
            deleteSuperBucket(buck, env);
            env_ma_free(compositionPreAlloc, env);
            env_ma_free(permCompBSPreAlloc, env);
            env_ma_free(block, env);
            break;
          }
          /* 6. dealloc resources no longer required */
          destructAppendState(&aState, env);
          deleteSuperBucket(buck, env);
        }
        /* 6. dealloc resources no longer required */
        env_ma_free(compositionPreAlloc, env);
        env_ma_free(permCompBSPreAlloc, env);
        env_ma_free(block, env);
      }
    } while(0);
    /* close bwttab and suffix array */
    if(hadError)
      newBlockEncIdxSeqErrRet();
  }
  freesuffixarray(&suffixArray, env);
  return &(newSeqIdx->baseClass);
}

static void
deleteBlockEncIdxSeq(struct encIdxSeq *seq, Env *env)
{
  struct blockCompositionSeq *bseq;
  assert(seq && seq->classInfo == &blockCompositionSeqClass);
  bseq = encIdxSeq2blockCompositionSeq(seq);
  destructOnDiskBlockCompIdx(&bseq->externalData, env);
  deleteCompositionList(bseq->compositionTable, env);
  MRAEncDelete(bseq->alphabet, env);
  MRAEncDelete(bseq->blockMapAlphabet, env);
  deleteSeqRangeList(bseq->rangeEncs, env);
  env_ma_free(bseq->modes, env);
  env_ma_free(bseq, env);
}

struct superBlock
{
  Seqpos *prevBucket;
  unsigned varIdxMemBase;
  BitString varIdx, compIdx;
};

static struct superBlock *
newEmptySuperBlock(size_t symsInBucket, size_t cwBitElems,
                   size_t varMaxBitElems, Env *env)
{
  struct superBlock *sBlock;
  size_t offset;
  sBlock = env_ma_calloc(env,
                         sizeof(*sBlock)
                         + symsInBucket * sizeof(Seqpos)
                         + sizeof(BitElem) * (cwBitElems + varMaxBitElems), 1);
  sBlock->prevBucket = (Seqpos *)((char *)sBlock + (offset = sizeof(*sBlock)));
  sBlock->compIdx = (BitString)((char *)sBlock
                               + (offset += symsInBucket * sizeof(Seqpos)));
  sBlock->varIdx = (BitString)((char *)sBlock
                               + (offset += sizeof(BitElem) * cwBitElems));
  return sBlock;
}

static void
deleteSuperBlock(struct superBlock *sBlock, Env *env)
{
/*   env_ma_free(sBlock->compIdx, env); */
/*   env_ma_free(sBlock->varIdx, env); */
/*   env_ma_free(sBlock->prevBucket, env); */
  env_ma_free(sBlock, env);
}

static inline Seqpos
blockNumFromPos(struct blockCompositionSeq *seqIdx, Seqpos pos)
{
  return pos / seqIdx->blockSize;
}

static inline Seqpos
superBucketNumFromPos(struct blockCompositionSeq *seqIdx, Seqpos pos)
{
  return pos / seqIdx->superBucketLen;
}

static inline Seqpos
superBucketBasePos(struct blockCompositionSeq *seqIdx, Seqpos superBucketNum)
{
  return superBucketNum * seqIdx->superBucketLen;
}


static inline Seqpos
superBucketNumFromBlockNum(struct blockCompositionSeq *seqIdx, Seqpos blockNum)
{
  return blockNum / (seqIdx->superBucketLen / seqIdx->blockSize);
}


#define fetchSuperBlockErrRet()                 \
  do {                                          \
    deleteSuperBlock(retval, env);              \
    return NULL;                                \
  } while(0)

static struct superBlock *
fetchSuperBlock(struct blockCompositionSeq *seqIdx, Seqpos superBucketNum,
                Env *env)
{
  size_t cwBitElems, bucketDataSize, blockEncNumSyms,
    varMaxBitElems;
  off_t superBucketDiskSize;
  unsigned superBucketLen, superBucketBlocks,
    blockSize, compositionIdxBits, maxVarIdxBits;
  struct superBlock *retval = NULL;
  FILE *idxFP;
  assert(seqIdx);
  blockSize = seqIdx->blockSize;
  idxFP = seqIdx->externalData.IdxData;
  superBucketBlocks = (superBucketLen = seqIdx->superBucketLen)/blockSize;
  assert(superBucketNum * superBucketLen < seqIdx->baseClass.seqLen);
  blockEncNumSyms = seqIdx->blockEncNumSyms;
  bucketDataSize = blockEncNumSyms * sizeof(Seqpos);
  compositionIdxBits = seqIdx->compositionTable->compositionIdxBits;
  superBucketDiskSize =
    superBucketSize(compositionIdxBits, blockEncNumSyms,
                    superBucketLen, blockSize);
  cwBitElems = bitElemsAllocSize(compositionIdxBits * superBucketBlocks);
  maxVarIdxBits = seqIdx->compositionTable->maxPermIdxBits;
  varMaxBitElems = bitElemsAllocSize(maxVarIdxBits * superBucketBlocks
                                     + bitElemBits - 1);
  retval = newEmptySuperBlock(blockEncNumSyms, cwBitElems, varMaxBitElems, env);
  if(superBucketNum != 0)
  {
    off_t prevBucketOffset = superBucketNum * superBucketDiskSize
      - bucketDataSize - sizeof(BitOffset);
    BitOffset varIdxOffset;
    if(fseeko(idxFP, seqIdx->externalData.cwIdxDataPos + prevBucketOffset,
              SEEK_SET))
      fetchSuperBlockErrRet();
    if(fread(retval->prevBucket, sizeof(Seqpos), blockEncNumSyms,
             idxFP) != blockEncNumSyms)
      fetchSuperBlockErrRet();
    if(fread(&varIdxOffset, sizeof(BitOffset), 1, idxFP) != 1)
      fetchSuperBlockErrRet();
    if(fread(retval->compIdx, sizeof(BitElem), cwBitElems, idxFP) != cwBitElems)
      fetchSuperBlockErrRet();
    if(fseeko(idxFP, seqIdx->externalData.varIdxDataPos
              + varIdxOffset/bitElemBits * sizeof(BitElem), SEEK_SET))
      fetchSuperBlockErrRet();
    retval->varIdxMemBase = varIdxOffset%bitElemBits;
    fread(retval->varIdx, sizeof(BitElem), varMaxBitElems, idxFP);
    if(ferror(idxFP))
      fetchSuperBlockErrRet();
  }
  else
  {
    if(fseeko(idxFP, seqIdx->externalData.cwIdxDataPos, SEEK_SET))
      fetchSuperBlockErrRet();
    if(fread(retval->compIdx, sizeof(BitElem), cwBitElems, idxFP) != cwBitElems)
      fetchSuperBlockErrRet();
    if(fseeko(idxFP, seqIdx->externalData.varIdxDataPos, SEEK_SET))
      fetchSuperBlockErrRet();
    fread(retval->varIdx, sizeof(BitElem), varMaxBitElems, idxFP);
  }
  return retval;
}

static Seqpos
blockCompSeqSelect(struct encIdxSeq *seq, Symbol sym, Seqpos count,
                   union EISHint *hint, Env *env)
{
  /* FIXME: implementation pending */
  return 0;
}

static Symbol *
blockCompSeqGetBlock(struct blockCompositionSeq *seqIdx, Seqpos blockNum,
                     struct blockEncIdxSeqHint *hint, int queryRangeEnc,
                     struct superBlock *sBlockPreFetch, Env *env)
{
  struct superBlock *sBlock;
  BitOffset varIdxMemOffset, cwIdxMemOffset = 0;
  Seqpos relBlockNum;
  unsigned blockSize, bitsPerCompositionIdx;
  Symbol *block;
  assert(seqIdx);
  blockSize = seqIdx->blockSize;
  if(blockNum * blockSize >= seqIdx->baseClass.seqLen)
    return NULL;
  if(sBlockPreFetch)
    sBlock = sBlockPreFetch;
  else
  {
    Seqpos superBucketNum = superBucketNumFromBlockNum(seqIdx, blockNum);
    sBlock = fetchSuperBlock(seqIdx, superBucketNum, env);
  }
  varIdxMemOffset = sBlock->varIdxMemBase;
  relBlockNum = blockNum % (seqIdx->superBucketLen/blockSize);
  bitsPerCompositionIdx = seqIdx->compositionTable->compositionIdxBits;
  block = env_ma_calloc(env, sizeof(Symbol), blockSize);
  while(relBlockNum)
  {
    size_t compIndex;
    compIndex = bsGetUInt64(sBlock->compIdx, cwIdxMemOffset,
                            bitsPerCompositionIdx);
    varIdxMemOffset +=
      seqIdx->compositionTable->permutations[compIndex].permIdxBits;
    cwIdxMemOffset += bitsPerCompositionIdx;
    --relBlockNum;
  }
  {
    size_t compPermIndex[2];
    unsigned varIdxBits,
      bitsPerPermutation = seqIdx->compositionTable->bitsPerSymbol * blockSize;
    struct permList *permutationList;
    compPermIndex[0] = bsGetUInt64(sBlock->compIdx, cwIdxMemOffset,
                                   bitsPerCompositionIdx);
    permutationList = seqIdx->compositionTable->permutations + compPermIndex[0];
    varIdxBits = permutationList->permIdxBits;
    compPermIndex[1] = bsGetUInt64(sBlock->varIdx, varIdxMemOffset, varIdxBits);
    bsGetUniformUIntArray(permutationList->catPerms,
                          bitsPerPermutation * compPermIndex[1],
                          seqIdx->compositionTable->bitsPerSymbol, blockSize,
                          block);
  }
  if(queryRangeEnc)
  {
    struct seqRange *nextRange;
    Seqpos inSeqPos = blockNum * blockSize;
    size_t i = 0;
    nextRange = SRLFindPositionNext(seqIdx->rangeEncs, inSeqPos,
                                    &hint->rangeHint);
    Seqpos inSeqStartPos = inSeqPos;
    do {
      if(inSeqPos < nextRange->startPos)
      {
        inSeqPos = nextRange->startPos;
      }
      else
      {
        unsigned maxSubstInBlockPos =
          MIN(nextRange->startPos + nextRange->len,
              inSeqStartPos + blockSize)  - inSeqStartPos;
        for(i = inSeqPos - inSeqStartPos; i < maxSubstInBlockPos; ++i)
        {
          block[i] = nextRange->sym;
        }
        inSeqPos = inSeqStartPos + maxSubstInBlockPos;
        ++nextRange;
      }
    } while(inSeqPos < inSeqStartPos + blockSize);
  }
  if(!sBlockPreFetch)
    deleteSuperBlock(sBlock, env);
  return block;
}

/* Note: since pos is meant inclusively, most computations use pos + 1 */
static Seqpos
blockCompSeqRank(struct encIdxSeq *eSeqIdx, Symbol sym, Seqpos pos,
                 union EISHint *hint, Env *env)
{
  struct blockCompositionSeq *seqIdx;
  Seqpos blockNum, rankCount;
  unsigned blockSize;
  Symbol eSym;
  assert(eSeqIdx && env && eSeqIdx->classInfo == &blockCompositionSeqClass);
  seqIdx = encIdxSeq2blockCompositionSeq(eSeqIdx);
  eSym = MRAEncMapSymbol(seqIdx->alphabet, sym);
  assert(MRAEncSymbolIsInSelectedRanges(seqIdx->alphabet,
                                        eSym, BLOCK_COMPOSITION_INCLUDE,
                                        seqIdx->modes) >= 0);
  if(MRAEncSymbolIsInSelectedRanges(seqIdx->alphabet, eSym,
                                    BLOCK_COMPOSITION_INCLUDE, seqIdx->modes))
  {
    BitOffset varIdxMemOffset, cwIdxMemOffset = 0;
    struct superBlock *sBlock;
    Symbol bSym = MRAEncMapSymbol(seqIdx->blockMapAlphabet, eSym);
    Seqpos superBucketNum;
    unsigned bitsPerCompositionIdx
      = seqIdx->compositionTable->compositionIdxBits;
    blockSize = seqIdx->blockSize;
    superBucketNum = superBucketNumFromPos(seqIdx, pos + 1);
    sBlock = fetchSuperBlock(seqIdx, superBucketNum, env);
    varIdxMemOffset = sBlock->varIdxMemBase;
    rankCount = sBlock->prevBucket[bSym];
    blockNum = blockNumFromPos(seqIdx, pos + 1);
    {
      /* if pos refers to last symbol in block, one can use the
       * composition count for the last block too */
      Seqpos relBlockNum = blockNum % (seqIdx->superBucketLen / blockSize);
      while(relBlockNum)
      {
        size_t compIndex;
        compIndex = bsGetUInt64(sBlock->compIdx, cwIdxMemOffset,
                                bitsPerCompositionIdx);
        rankCount += symCountFromComposition(seqIdx, compIndex, bSym);
        varIdxMemOffset +=
          seqIdx->compositionTable->permutations[compIndex].permIdxBits;
        cwIdxMemOffset += bitsPerCompositionIdx;
        --relBlockNum;
      }
    }
    {
      Seqpos inBlockPos;
      if((inBlockPos = (pos + 1) % blockSize)
         && symCountFromComposition(seqIdx,
                                    bsGetUInt64(sBlock->compIdx, cwIdxMemOffset,
                                                bitsPerCompositionIdx), bSym))
      {
        Symbol *block;
        unsigned i;
        /* PERFORMANCE: pull blockCompSeqGetBlock parts into here,
         * there's much in common anyway */
        block = blockCompSeqGetBlock(seqIdx, blockNum, &hint->bcHint, 0, sBlock,
                                     env);
        for(i = 0; i < inBlockPos; ++i)
        {
          if(block[i] == eSym)
            ++rankCount;
        }
        env_ma_free(block, env);
      }
      if(bSym == seqIdx->fallback)
      {
        Seqpos base = superBucketBasePos(seqIdx, superBucketNum);
        rankCount -= SRLAllSymbolsCountInSeqRegion(seqIdx->rangeEncs, base, pos,
                                                   &hint->bcHint.rangeHint);
      }
    }
    deleteSuperBlock(sBlock, env);
  }
  else
  {
    /* FIXME: rank for range symbols */
    rankCount = SRLSymbolCountInSeqRegion(seqIdx->rangeEncs, 0, pos, eSym,
                                          &hint->bcHint.rangeHint);
  }
  return rankCount;
}

static Symbol
blockCompSeqGet(struct encIdxSeq *seq, Seqpos pos, union EISHint *hint,
                Env *env)
{
  Symbol sym, *block;
  struct blockCompositionSeq *seqIdx;
  unsigned blockSize;
  assert(seq && seq->classInfo == &blockCompositionSeqClass);
  if(pos >= seq->seqLen)
    return ~(Symbol)0;
  seqIdx = encIdxSeq2blockCompositionSeq(seq);
  blockSize = seqIdx->blockSize;
  block = blockCompSeqGetBlock(seqIdx, pos/blockSize, &(hint->bcHint),
                               1, NULL, env);
  MRAEncSymbolsRevTransform(seqIdx->alphabet, block, blockSize);
  sym = block[pos%blockSize];
  env_ma_free(block, env);
  return sym;
}

#if 1
/**
 * Setup composition array for lexically maximal composition.
 * @param maxSym last symbol to represent (lexical maximum)
 * @param blockSize sum of symbol counts in composition
 * (i.e. composition contains this many symbols)
 * @param composition vector of symbol counts
 * @param symRMNZ points to index of rightmost non-zero symbol count
 * in composition vector (state of generating procedure)
 */
static inline void
initComposition(Symbol maxSym, unsigned blockSize,
                unsigned composition[maxSym + 1], unsigned *symRMNZ)
{
  memset(composition, 0, sizeof(composition[0]) * (maxSym));
  composition[*symRMNZ = maxSym] = blockSize;
}
#else
/**
 * Setup composition array for lexically minimal composition.
 * @param maxSym last symbol to represent (lexical maximum)
 * @param blockSize sum of symbol counts in composition
 * (i.e. composition contains this many symbols)
 * @param composition vector of symbol counts
 * @param symRMNZ points to index of rightmost non-zero symbol count
 * in composition vector (state of generating procedure)
 */
static inline void
initComposition(Symbol maxSym, unsigned blockSize,
                unsigned composition[maxSym + 1], unsigned *symLMNZ)
{
  memset(composition + 1, 0, sizeof(composition[0]) * (maxSym));
  composition[*symLMNZ = 0] = blockSize;
}
#endif

#if 1
/**
 * Compute lexically previous composition given any composition in
 * \link composition \endlink.
 * @param composition array[0..maxSym] of occurrence counts for each symbol
 * @param maxSym last symbol index of composition
 * @param symMNZ rightmost non-zero entry of composition[]
 */
static inline void
nextComposition(unsigned composition[],
                Symbol maxSym,
                unsigned *symRMNZ)
{
  ++composition[*symRMNZ - 1];
  if(!(--composition[*symRMNZ]))
  {
    --*symRMNZ;
  }
  else if(!(composition[maxSym]))
  {
    composition[maxSym] = composition[*symRMNZ];
    composition[*symRMNZ] = 0;
    *symRMNZ = maxSym;
  }
}
#else
/**
 * Compute lexically next composition given any composition in
 * \link composition \endlink.
 * @param composition array[0..maxSym] of occurrence counts for each symbol
 * @param maxSym last symbol index of composition
 * @param symMNZ rightmost non-zero entry of composition[]
 */
static inline void
nextComposition(unsigned composition[],
                Symbol maxSym,
                unsigned *symLMNZ)
{
  ++composition[*symLMNZ + 1];
  if(!(--composition[*symLMNZ]))
  {
    ++*symLMNZ;
  }
  else if(!(composition[0]))
  {
    composition[0] = composition[*symLMNZ];
    composition[*symLMNZ] = 0;
    *symLMNZ = 0;
  }
}
#endif

#define newCompositionListErrRet()                                      \
  do {                                                                  \
    if(newList->permutations)                                           \
    {                                                                   \
      unsigned i;                                                       \
      for(i = 0; i < newList->numCompositions; ++i)                     \
        if(newList->permutations[i].catPerms)                           \
          env_ma_free(newList->permutations[i].catPerms, env);          \
      env_ma_free(newList->permutations, env);                          \
    }                                                                   \
    if(newList->catComps) env_ma_free(newList->catComps, env);          \
    if(newList) env_ma_free(newList, env);                              \
    if(composition) env_ma_free(composition, env);                      \
    return NULL;                                                        \
  } while(0)

static int
initPermutationsList(const unsigned *composition, struct permList *permutation,
                     unsigned blockSize, Symbol numSyms,
                     unsigned bitsPerSymbol, Env *env);
#if DEBUG > 1
static void
printComposition(FILE *fp, const unsigned *composition, Symbol numSyms,
                 unsigned blockSize);
#endif /* DEBUG > 1 */

/**
 * @return NULL on error
 */
static struct compList *
newCompositionList(const struct blockCompositionSeq *seqIdx,
                   Symbol numSyms, Env *env)
{
  struct compList *newList = NULL;
  unsigned *composition = NULL;
  unsigned blockSize;
  Symbol maxSym = numSyms - 1;
  BitOffset bitsPerComp, bitsPerCount;
  size_t numCompositions;
  size_t maxNumPermutations = 0;
  assert(seqIdx);
  blockSize = seqIdx->blockSize;
  if(!(composition =
       env_ma_malloc(env, sizeof(composition[0]) * blockSize)))
    newCompositionListErrRet();
  if(!(newList =
       env_ma_calloc(env, 1, sizeof(struct compList))))
    newCompositionListErrRet();
  bitsPerComp = numSyms * (bitsPerCount = requiredUIntBits(blockSize));
  newList->bitsPerCount = bitsPerCount;
  numCompositions = newList->numCompositions =
    binomialCoeff(blockSize + maxSym, maxSym);
  newList->compositionIdxBits = requiredUInt64Bits(numCompositions - 1);
  newList->bitsPerSymbol = requiredUIntBits(maxSym);

  newList->catComps = env_ma_malloc(env, 
     bitElemsAllocSize(numCompositions * bitsPerComp) * sizeof(BitElem));
  newList->permutations = 
    env_ma_calloc(env, sizeof(struct permList), numCompositions);
  {
    unsigned symRMNZ;
    BitOffset offset = 0;
    size_t i = 0;
#ifndef NDEBUG
    size_t permSum = 0;
#endif
    initComposition(maxSym, blockSize, composition, &symRMNZ);
    do
    {
#if DEBUG > 1
      printComposition(stderr, composition, numSyms, blockSize);
#endif /* DEBUG > 1 */
      bsStoreUniformUIntArray(newList->catComps, offset,
                              bitsPerCount, numSyms, composition);
      assert(i > 1?(bsCompare(newList->catComps, offset, bitsPerComp,
                              newList->catComps, offset - bitsPerComp,
                              bitsPerComp)>0):1);
      if(initPermutationsList(composition, newList->permutations + i,
                              blockSize, numSyms, newList->bitsPerSymbol, env))
        newCompositionListErrRet();
#ifndef NDEBUG
      env_log_log(env, "%lu",
                  (unsigned long)newList->permutations[i].numPermutations);
      permSum += newList->permutations[i].numPermutations;
#endif
      if(newList->permutations[i].numPermutations > maxNumPermutations)
        maxNumPermutations = newList->permutations[i].numPermutations;
      if(++i < numCompositions)
        ;
      else
        break;
      offset += bitsPerComp;
      nextComposition(composition, maxSym, &symRMNZ);
    } while(1);
    /* verify that the last composition is indeed the lexically maximally */
    assert(composition[0] == blockSize);
#ifndef NDEBUG
    env_log_log(env, "permSum=%lu, numSyms=%lu, blockSize=%d, "
                "pow(numSyms, blockSize)=%f",
                (unsigned long)permSum, (unsigned long)numSyms, blockSize,
                pow(numSyms, blockSize));
#endif
    assert(permSum == pow(numSyms, blockSize));
  }
  newList->maxPermCount = maxNumPermutations;
  newList->maxPermIdxBits = requiredUInt64Bits(maxNumPermutations - 1);
  env_ma_free(composition, env);
  return newList;
}

static void
deleteCompositionList(struct compList *clist, Env *env)
{
  {
    unsigned i;
    for(i = 0; i < clist->numCompositions; ++i)
      env_ma_free(clist->permutations[i].catPerms, env);
  }
  env_ma_free(clist->permutations, env);
  env_ma_free(clist->catComps, env);
  env_ma_free(clist, env);
}

#if DEBUG > 1
static void
printComposition(FILE *fp, const unsigned *composition,
                 Symbol numSyms, unsigned blockSize)
{
  Symbol sym;
  unsigned width = digitPlaces(blockSize, 10);
  for(sym = 0; sym < numSyms; ++sym)
  {
    fprintf(fp, "%*d ", width, composition[sym]);
  }
  fputs("\n", fp);
}
#endif /* DEBUG > 1 */

static unsigned
symCountFromComposition(const struct blockCompositionSeq *seqIdx,
                        size_t compIndex, Symbol sym)
{
  BitOffset bitsPerComp, bitsPerCount;
  assert(seqIdx);
  bitsPerCount = seqIdx->compositionTable->bitsPerCount;
  bitsPerComp = bitsPerCount * MRAEncGetSize(seqIdx->blockMapAlphabet);
  assert(compIndex < seqIdx->compositionTable->numCompositions);
  return bsGetUInt(seqIdx->compositionTable->catComps,
                   compIndex * bitsPerComp + sym * bitsPerCount,
                   bitsPerCount);
}

static void
initPermutation(Symbol *permutation, const unsigned *composition,
                unsigned blockSize, Symbol numSyms);
static void
nextPermutation(Symbol *permutation, unsigned blockSize);

#if DEBUG > 1
static void
printPermutation(FILE *fp, Symbol *permutation, unsigned blockSize);
#endif /* DEBUG > 1 */


static int
initPermutationsList(const unsigned *composition, struct permList *permutation,
                     unsigned blockSize, Symbol numSyms,
                     unsigned bitsPerSymbol, Env *env)
{
  size_t numPermutations = permutation->numPermutations =
    multinomialCoeff(blockSize, numSyms, composition);
  permutation->permIdxBits = requiredUInt64Bits(numPermutations - 1);
  Symbol *currentPermutation;
  BitOffset bitsPerPermutation = bitsPerSymbol * blockSize;
  if(!(permutation->catPerms =
       env_ma_malloc(env, bitElemsAllocSize(
                       (BitOffset)numPermutations *
                       bitsPerPermutation)
                     * sizeof(BitElem))))
    return -1;
  if(!(currentPermutation = env_ma_malloc(env, 
         sizeof(Symbol) * blockSize)))
  {
    env_ma_free(permutation->catPerms, env);
    return -1;
  }
  initPermutation(currentPermutation, composition, blockSize, numSyms);
  {
    size_t i = 0;
    BitOffset offset = 0;
    do
    {
      bsStoreUniformUIntArray(permutation->catPerms, offset,
                              bitsPerSymbol, blockSize, currentPermutation);
#if DEBUG > 1
      printPermutation(stderr, currentPermutation, blockSize);
#endif /* DEBUG > 1 */
      assert(i > 0?(bsCompare(permutation->catPerms, offset, bitsPerPermutation,
                              permutation->catPerms,
                              offset - bitsPerPermutation,
                              bitsPerPermutation)>0):1);
      if(++i < numPermutations)
        ;
      else
        break;
      offset += bitsPerPermutation;
      nextPermutation(currentPermutation, blockSize);
    } while(1);
  }
  env_ma_free(currentPermutation, env);
  return 0;
}

static void
initPermutation(Symbol *permutation, const unsigned *composition,
                unsigned blockSize, Symbol numSyms)
{
  Symbol sym, *p = permutation;
  unsigned j;
  for(sym = 0; sym < numSyms; ++sym)
    for(j = 0; j < composition[sym]; ++j)
      *(p++) = sym;
}

static void
nextPermutation(Symbol *permutation, unsigned blockSize)
{
  /*
   * Every permutation represents a leaf in the decision tree
   * generating all permutations, thus given one permutation, we
   * ascend in the tree until we can make a decision resulting in a
   * lexically larger value.
   */
  unsigned upLvl = blockSize - 1;
  {
    Symbol maxSymInSuf = permutation[blockSize - 1];
    do
    {
      if(upLvl == 0 || maxSymInSuf > permutation[--upLvl])
        break;
      if(permutation[upLvl] > maxSymInSuf)
        maxSymInSuf = permutation[upLvl];
    } while(1);
  }
  /* now that we have found the branch point, descend again,
   * selecting first the next largest symbol */
  {
    Symbol saved = permutation[upLvl];
    Symbol swap = permutation[upLvl + 1];
    unsigned swapIdx = upLvl + 1;
    unsigned i;
    for(i = swapIdx + 1; i < blockSize; ++i)
    {
      if(permutation[i] > saved && permutation[i] < swap)
        swap = permutation[i], swapIdx = i;
    }
    permutation[upLvl] = swap;
    permutation[swapIdx] = saved;
  }
  for(++upLvl;upLvl < blockSize - 1; ++upLvl)
  {
    /* find minimal remaining symbol */
    unsigned i, minIdx = upLvl;
    Symbol minSym = permutation[upLvl];
    for(i = upLvl + 1; i < blockSize; ++i)
      if(permutation[i] < minSym)
        minSym = permutation[i], minIdx = i;
    permutation[minIdx] = permutation[upLvl];
    permutation[upLvl] = minSym;
  }
}

#if DEBUG > 1
static void
printPermutation(FILE *fp, Symbol *permutation, unsigned blockSize)
{
  unsigned i;
  if(blockSize)
    fprintf(fp, "%lu", (unsigned long)permutation[0]);
  for(i = 1; i < blockSize; ++i)
  {
    fprintf(fp, "%lu", (unsigned long)permutation[i]);
  }
  fputs("\n", fp);
}
#endif /* DEBUG > 1 */

/**
 * \brief Transforms a block-sized sequence of symbols to corresponding
 * index-pair representation of block-composition index.
 * @param seqIdx sequence index object holding tables to use for
 * lookup
 * @param block symbol sequence holding at least as many symbols as
 * required by seqIdx
 * @param idxOutput on return idxOutput[0] holds the composition
 * index, idxOutput[1] the permutation index.
 * @param bitsOfPermIdx if non-NULL, the variable pointed to holds the
 * number of significant bits for the permutation index.
 * @param env Environment to use for memory allocation etc.
 * @param permCompPA if not NULL must point to a memory region of
 * sufficient size to hold the concatenated bistring representations
 * of composition and permutation, composition at offset 0,
 * permutation at offset
 * seqIdx->compositionTable->bitsPerCount * seqIdx->blockEncNumSyms.
 * @param compPA if not NULL must reference a memory region of
 * sufficient size to hold the composition representation as alphabet
 * range sized sequence.
 * @return -1 in case of memory exhaustion, cannot happen if both
 * permCompPA and compPA are valid preallocated memory regions, 0
 * otherwise.
 */
static int
block2IndexPair(const struct blockCompositionSeq *seqIdx,
                const Symbol *block,
                size_t idxOutput[2],
                unsigned *bitsOfPermIdx,
                Env *env,
                BitString permCompPA, unsigned *compPA)
{
  unsigned blockSize, bitsPerCount;
  BitOffset bitsPerComposition, bitsPerPermutation;
  Symbol numSyms;
  struct compList *compositionTab;
  BitString permCompBitString;
  assert(seqIdx && idxOutput && block);
  assert(seqIdx->blockSize > 0);
  compositionTab = seqIdx->compositionTable;
  blockSize = seqIdx->blockSize;
  bitsPerComposition = (bitsPerCount = compositionTab->bitsPerCount)
    * (numSyms = seqIdx->blockEncNumSyms);
  bitsPerPermutation = compositionTab->bitsPerSymbol * blockSize;
  if(permCompPA)
    permCompBitString = permCompPA;
  else
    permCompBitString =
      env_ma_malloc(env,
                    bitElemsAllocSize(bitsPerComposition + bitsPerPermutation)
                    * sizeof(BitElem));
  {
    /* first compute composition from block */
    size_t i;
    unsigned *composition;
    if(compPA)
    {
      composition = compPA;
      memset(composition, 0, sizeof(composition[0])*seqIdx->blockEncNumSyms);
    }
    else
      composition = env_ma_calloc(env, sizeof(composition[0]),
                                  seqIdx->blockEncNumSyms);
    for(i = 0; i < blockSize; ++i)
    {
      ++composition[block[i]];
    }
    bsStoreUniformUIntArray(permCompBitString, 0, bitsPerCount,
                            numSyms, composition);
    if(!compPA)
      env_ma_free(composition, env);
  }
  {
    /* do binary search for composition (simplified because the list
     * contains every composition possible and thus cmpresult will
     * become 0 at some point) */
    size_t compIndex = compositionTab->numCompositions / 2;
    size_t divStep = compIndex;
    int cmpresult;
    while((cmpresult = bsCompare(permCompBitString, 0, bitsPerComposition,
                                 compositionTab->catComps,
                                 compIndex * bitsPerComposition,
                                 bitsPerComposition)))
    {
      if(divStep > 1)
        divStep >>= 1; /* divStep /= 2 */
      if(cmpresult > 0)
        compIndex += divStep;
      else /* cmpresult < 0 */
        compIndex -= divStep;
    }
    idxOutput[0] = compIndex;
  }
  {
    struct permList *permutation = compositionTab->permutations
      + idxOutput[0];
    if(bitsOfPermIdx)
      *bitsOfPermIdx = permutation->permIdxBits;
    if(permutation->numPermutations > 1)
    {
      /* build permutation bitstring */
      bsStoreUniformUIntArray(permCompBitString, bitsPerComposition,
                              compositionTab->bitsPerSymbol, blockSize, block);
      /* do binary search for permutation */
      {
        size_t permIndex = permutation->numPermutations / 2;
        size_t divStep = permIndex;
        int cmpresult;
        while((cmpresult = bsCompare(permCompBitString, bitsPerComposition,
                                     bitsPerPermutation,
                                     permutation->catPerms,
                                     permIndex * bitsPerPermutation,
                                     bitsPerPermutation)))
        {
          if(divStep > 1)
            divStep >>= 1; /* divStep /= 2 */
          if(cmpresult > 0)
            permIndex += divStep;
          else /* cmpresult < 0 */
            permIndex -= divStep;
        }
        idxOutput[1] = permIndex;
      }
    }
    else
        idxOutput[1] = 0;
  }
  if(!permCompPA)
    env_ma_free(permCompBitString, env);
  return 0;
}

/**
 * @return -1 in case of memory exhaustion, 0 otherwise.
 */
extern int
searchBlock2IndexPair(const struct encIdxSeq *seqIdx,
                      const Symbol *block,
                      size_t idxOutput[2], Env *env)
{
  return block2IndexPair(constEncIdxSeq2blockCompositionSeq(seqIdx),
                         block, idxOutput, NULL, env, NULL, NULL);
}

static inline superBucket
newSuperBucket(size_t alphabetSize, Env *env)
{
  return env_ma_calloc(env, alphabetSize, sizeof(Seqpos));
}

static inline void
deleteSuperBucket(superBucket buck, Env *env)
{
  env_ma_free(buck, env);
}

static void
addBlock2SuperBucket(superBucket supB, Symbol *block, size_t blockSize)
{
  size_t i;
  for(i = 0; i < blockSize; ++i)
    ++(supB[block[i]]);
}

static inline size_t
superBucketSize(unsigned compositionIdxBits, unsigned alphabetSize,
                size_t superBucketLen, unsigned blockSize)
{
  return bitElemsAllocSize(compositionIdxBits * superBucketLen/blockSize)
    * sizeof(BitElem) + sizeof(Seqpos) * alphabetSize + sizeof(BitOffset);
}

static inline off_t
cwSize(Seqpos seqLen, unsigned compositionIdxBits,
       unsigned alphabetSize, size_t superBucketLen, unsigned blockSize)
{
  off_t cwLen;
  Seqpos numSuperBuckets = seqLen / superBucketLen
    + ((seqLen % superBucketLen)?1:0);
  cwLen = superBucketSize(compositionIdxBits, alphabetSize, superBucketLen,
                          blockSize) * numSuperBuckets;
  return cwLen;
}


/**
 * @return -1 on error, 0 otherwise
 */
static int
initOnDiskBlockCompIdx(struct onDiskBlockCompIdx *idx,
                       Str *projectName, off_t cwLen,
                       Env *env)
{
  Str *outputName = str_clone(projectName, env);
  str_append_cstr(outputName, ".bdx", env);
  idx->IdxData = env_fa_fopen(env, str_get(outputName), "wb+");
  idx->cwIdxDataPos = 0;
  idx->varIdxDataPos = cwLen;
  idx->rangeEncPos = 0;
  str_delete(outputName, env);
  return 0;
}

static void
destructOnDiskBlockCompIdx(struct onDiskBlockCompIdx *idx, Env *env)
{
  env_fa_xfclose(idx->IdxData, env);
}

static void
initAppendState(struct appendState *aState,
                BitOffset compCacheLen, BitOffset permCacheLen,
                Env *env)
{
  aState->compCacheLen = compCacheLen;
  aState->permCacheLen = permCacheLen;
  aState->compCache = env_ma_malloc(env, sizeof (BitElem) *
                                    bitElemsAllocSize(compCacheLen));
  aState->permCache = env_ma_malloc(env, sizeof (BitElem) *
                                    bitElemsAllocSize(permCacheLen));
  aState->cwMemPos = aState->cwDiskOffset = aState->varMemPos =
    aState->varDiskOffset = aState->varMemOldBits = 0;
}

static void
destructAppendState(struct appendState *aState, Env *env)
{
  env_ma_free(aState->permCache, env);
  env_ma_free(aState->compCache, env);
}

/**
 * @return 0 on successfull update, <0 on error
 */
static int
append2IdxOutput(struct blockCompositionSeq *newSeqIdx,
                 struct appendState *state,
                 size_t permCompIdx[2],
                 unsigned bitsOfCompositionIdx, unsigned bitsOfPermutationIdx,
                 Env *env)
{
  if(state->cwMemPos + bitsOfCompositionIdx > state->compCacheLen)
  {
    state->compCache =
      env_ma_realloc(env, state->compCache,
                     bitElemsAllocSize(
                       state->compCacheLen += bitsOfCompositionIdx));
  }
  bsStoreUInt64(state->compCache, state->cwMemPos, bitsOfCompositionIdx,
                permCompIdx[0]);
  state->cwMemPos += bitsOfCompositionIdx;
  if(state->varMemPos + bitsOfPermutationIdx > state->permCacheLen)
  {
    state->permCache =
      env_ma_realloc(env, state->permCache,
                     bitElemsAllocSize(
                       state->permCacheLen += bitsOfPermutationIdx));
  }
  bsStoreUInt64(state->permCache, state->varMemPos, bitsOfPermutationIdx,
                permCompIdx[1]);
  state->varMemPos += bitsOfPermutationIdx;
  return 0;
}

/**
 * @return >0 on success, 0 on I/O-error
 */
static int
updateIdxOutput(struct blockCompositionSeq *seqIdx,
                struct appendState *aState,
                superBucket buck)
{
  size_t recordsExpected, cwBitElems;
  BitOffset nextVarDiskOffset;
  /* FIXME: only save required bits of super-bucket data */
  /* seek2/write constant width indices */
  if(fseeko(seqIdx->externalData.IdxData,
            seqIdx->externalData.cwIdxDataPos + aState->cwDiskOffset,
            SEEK_SET))
    return 0;
  cwBitElems = recordsExpected = bitElemsAllocSize(aState->cwMemPos);
  if(recordsExpected != fwrite(aState->compCache, sizeof(BitElem),
                               recordsExpected, seqIdx->externalData.IdxData))
    return 0;
  /* append superbucket */
  recordsExpected = seqIdx->blockEncNumSyms;
  if(recordsExpected != fwrite(buck, sizeof(buck[0]),
                               recordsExpected, seqIdx->externalData.IdxData))
    return 0;
  /* append variable width offset position */
  recordsExpected = 1;
  nextVarDiskOffset = aState->varDiskOffset + aState->varMemPos
    - aState->varMemOldBits;
  if(recordsExpected != fwrite(&nextVarDiskOffset, sizeof(BitOffset),
                               recordsExpected, seqIdx->externalData.IdxData))
    return 0;
  /* seek2/write variable width indices */
  if(fseeko(seqIdx->externalData.IdxData,
            seqIdx->externalData.varIdxDataPos
            + aState->varDiskOffset/bitElemBits * sizeof(BitElem), SEEK_SET))
    return 0;
  recordsExpected = aState->varMemPos/bitElemBits;
  if(recordsExpected != fwrite(aState->permCache, sizeof(BitElem),
                               recordsExpected, seqIdx->externalData.IdxData))
    return 0;
  /* move last elem of permCache with unwritten bits to front */
  if(aState->varMemPos % bitElemBits)
    aState->permCache[0] = aState->permCache[recordsExpected];
  /* FIXME: this assumes that the string of variable width date does
   * indeed occupy at least one full bitElem */
  aState->cwDiskOffset +=
    superBucketSize(seqIdx->compositionTable->compositionIdxBits,
                    seqIdx->blockEncNumSyms, seqIdx->superBucketLen,
                    seqIdx->blockSize);
  aState->cwMemPos = 0;
  aState->varDiskOffset += aState->varMemPos - aState->varMemOldBits;
  aState->varMemOldBits = (aState->varMemPos %= bitElemBits);
  return 1;
}

/**
 * @return 0 on error, 1 on success
 */
static int
finalizeIdxOutput(struct blockCompositionSeq *seqIdx,
                  struct appendState *aState,
                  superBucket buck, Env *env)
{
  off_t rangeEncPos;
  size_t recordsExpected;
  assert(aState && seqIdx);
  assert(aState->varMemOldBits < bitElemBits
         && aState->varMemOldBits == aState->varMemPos);
  if(aState->varMemOldBits)
  {
    /* seek2/write variable width indices */
    if(fseeko(seqIdx->externalData.IdxData,
              seqIdx->externalData.varIdxDataPos
              + aState->varDiskOffset/bitElemBits * sizeof(BitElem), SEEK_SET))
      return 0;
    recordsExpected = 1;
    if(recordsExpected != fwrite(aState->permCache, sizeof(BitElem),
                                 recordsExpected,
                                 seqIdx->externalData.IdxData))
      return 0;
  }
  rangeEncPos = seqIdx->externalData.varIdxDataPos
    + aState->varDiskOffset / bitElemBits * sizeof(BitElem)
    + ((aState->varDiskOffset%bitElemBits)?sizeof(BitElem):0);
  seqIdx->externalData.rangeEncPos = rangeEncPos;
  /* insert terminator so every search for a next range will find a
   * range just beyond the sequence end */
  SRLAppendNewRange(seqIdx->rangeEncs, 
                    seqIdx->baseClass.seqLen + seqIdx->blockSize, 1, 0, env);
  SRLCompact(seqIdx->rangeEncs, env);
  if(fseeko(seqIdx->externalData.IdxData, rangeEncPos, SEEK_SET))
    return 0;
  if(!(SRLSaveToStream(seqIdx->rangeEncs, seqIdx->externalData.IdxData)))
     return 0;
  return 1;
}

static void
addRangeEncodedSyms(struct seqRangeList *rangeList, Symbol *block,
                    size_t blockSize,
                    Seqpos blockNum, MRAEnc *alphabet,
                    int selection, int *rangeSel, Env *env)
{
  size_t i;
  for(i = 0; i < blockSize; ++i)
  {
    assert(MRAEncSymbolIsInSelectedRanges(alphabet, block[i],
                                          selection, rangeSel) >= 0);
    if(MRAEncSymbolIsInSelectedRanges(alphabet, block[i], selection, rangeSel))
      SRLAddPosition(rangeList, blockNum * blockSize + i, block[i], env);
  }
}

static union EISHint *
newBlockCompSeqHint(struct encIdxSeq *seq, Env *env)
{
  union EISHint *hintret;
  struct blockCompositionSeq *seqIdx;
  assert(seq && seq->classInfo == &blockCompositionSeqClass);
  seqIdx = encIdxSeq2blockCompositionSeq(seq);
  hintret = env_ma_malloc(env, sizeof(union EISHint));
  SRLInitListSearchHint(seqIdx->rangeEncs, &hintret->bcHint.rangeHint);
  return hintret;
}

static void
deleteBlockCompSeqHint(struct encIdxSeq *seq, union EISHint *hint, Env *env)
{
  env_ma_free(hint, env);
}


static const struct encIdxSeqClass blockCompositionSeqClass =
{
  .delete = deleteBlockEncIdxSeq, 
  .rank = blockCompSeqRank,
  .select = blockCompSeqSelect,
  .get = blockCompSeqGet,
  .newHint = newBlockCompSeqHint,
  .deleteHint = deleteBlockCompSeqHint,
};
