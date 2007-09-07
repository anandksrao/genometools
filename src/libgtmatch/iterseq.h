#ifndef ITERSEQ_H
#define ITERSEQ_H
#include <inttypes.h>
#include "libgtcore/strarray.h"
#include "symboldef.h"
#include "arraydef.h"
#include "seqdesc.h"

typedef struct Scansequenceiterator Scansequenceiterator;

int overallquerysequences(int(*processsequence)(void *,
                                                uint64_t,
                                                const Uchar *,
                                                unsigned long,
                                                const char *,
                                                Env *),
                          void *info,
                          ArrayUchar *sequencebuffer,
                          const StrArray *filenametab,
                          Sequencedescription *sequencedescription,
                          const Uchar *symbolmap,
                          Env *env);

Scansequenceiterator *newScansequenceiterator(const StrArray *filenametab,
                                              const Uchar *symbolmap,
                                              Env *env);

void freeScansequenceiterator(Scansequenceiterator **sseqit,Env *env);

int nextScansequenceiterator(const Uchar **sequence,
                             unsigned long *len,
                             char **desc,
                             Scansequenceiterator *sseqit,
                             Env *env);

#endif
