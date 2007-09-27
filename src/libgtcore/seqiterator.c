/*
  Copyright (c) 2007 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg

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

#include "libgtcore/arraydef.h"
#include "libgtcore/chardef.h"
#include "libgtcore/fastabuffer.h"
#include "libgtcore/strarray.h"
#include "libgtcore/symboldef.h"
#include "libgtcore/seqiterator.h"

struct SeqIterator
{
  FastaBuffer *fb;
  const StrArray *filenametab;
  const Uchar *symbolmap;
  Queue *descptr;
  ArrayUchar sequencebuffer;
  unsigned long long unitnum;
  bool withsequence, exhausted;
};

SeqIterator* seqiterator_new(const StrArray *filenametab,
                             const Uchar *symbolmap,
                             bool withsequence,
                             Env *env)
{
  SeqIterator *seqit;
  env_error_check(env);
  seqit = env_ma_malloc(env, sizeof (SeqIterator));
  INITARRAY(&seqit->sequencebuffer, Uchar);
  seqit->descptr = queue_new(env);
  seqit->fb = fastabuffer_new(filenametab,
                               symbolmap,
                               false,
                               NULL,
                               seqit->descptr,
                               NULL,
                               env);
  seqit->exhausted = false;
  seqit->unitnum = 0;
  seqit->withsequence = withsequence;
  return seqit;
}

int seqiterator_next(SeqIterator *seqit,
                     const Uchar **sequence,
                     unsigned long *len,
                     char **desc,
                     Env *env)
{
  Uchar charcode;
  int retval;
  bool haserr = false, foundseq = false;

  assert(seqit && len && desc);
  assert((sequence && seqit->withsequence) || !seqit->withsequence);

  if (seqit->exhausted)
  {
    return 0;
  }
  while (true)
  {
    retval = fastabuffer_next(seqit->fb,&charcode,env);
    if (retval < 0)
    {
      haserr = true;
      break;
    }
    if (retval == 0)
    {
      seqit->exhausted = true;
      break;
    }
    if (charcode == (Uchar) SEPARATOR)
    {
      if (seqit->sequencebuffer.nextfreeUchar == 0 && seqit->withsequence)
      {
        env_error_set(env,"sequence %llu is empty", seqit->unitnum);
        haserr = true;
        break;
      }
      *desc = queue_get(seqit->descptr,env);
      *len = seqit->sequencebuffer.nextfreeUchar;
      if (seqit->withsequence)
      {
        *sequence = seqit->sequencebuffer.spaceUchar;
      }
      seqit->sequencebuffer.nextfreeUchar = 0;
      foundseq = true;
      seqit->unitnum++;
      break;
    } else
    {
      if (seqit->withsequence)
      {
        STOREINARRAY(&seqit->sequencebuffer, Uchar, 1024, charcode);
      }
      else
        seqit->sequencebuffer.nextfreeUchar++;
    }
  }
  if (!haserr && seqit->sequencebuffer.nextfreeUchar > 0)
  {
    *desc = queue_get(seqit->descptr,env);
    if (seqit->withsequence)
    {
      *sequence = seqit->sequencebuffer.spaceUchar;
    }
    *len = seqit->sequencebuffer.nextfreeUchar;
    foundseq = true;
    seqit->sequencebuffer.nextfreeUchar = 0;
  }
  if (haserr)
  {
    return -1;
  }
  if (foundseq)
  {
    return 1;
  }
  return 0;
}

void seqiterator_delete(SeqIterator *seqit, Env *env)
{
  if (!seqit) return;
  queue_delete_with_contents(seqit->descptr, env);
  fastabuffer_delete(seqit->fb, env);
  FREEARRAY(&seqit->sequencebuffer, Uchar);
  env_ma_free(seqit, env);
}

