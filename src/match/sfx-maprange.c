/*
  Copyright (c) 2011 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2011 Center for Bioinformatics, University of Hamburg

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

#include <errno.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#include "core/xposix.h"
#include "core/xansi_api.h"
#endif
#include "core/fa.h"
#include "core/intbits.h"
#include "core/log.h"
#include "sfx-maprange.h"

static unsigned long gt_multipleofpagesize(unsigned long code,
                                           bool smaller,
                                           size_t sizeofbasetype,
                                           unsigned long pagesize)
{
  if ((code * sizeofbasetype) % pagesize == 0)
  {
    return code * sizeofbasetype;
  }
  if (smaller)
  {
    return ((code * sizeofbasetype)/pagesize) * pagesize;
  }
  return ((code * sizeofbasetype)/pagesize) * pagesize + pagesize;
}

typedef struct
{
  unsigned long mapoffset, mapend;
} GtMappedrange;

static void gt_Sfxmapped_offset_end(GtMappedrange *range,
                                    size_t sizeofbasetype,
                                    unsigned long pagesize,
                                    unsigned long minindex,
                                    unsigned long maxindex)
{
  range->mapoffset = gt_multipleofpagesize(minindex,true,sizeofbasetype,
                                           pagesize);
  range->mapend = gt_multipleofpagesize(maxindex,false,sizeofbasetype,pagesize);
}

struct GtSfxmappedrange
{
  void *ptr, *entire, **usedptrptr;
  GtStr *filename, *tablename;
  unsigned long pagesize;
  size_t numofunits, sizeofunit;
  GtSfxmappedrangetype type;
  GtSfxmappedrangetransformfunc transformfunc;
  unsigned long currentminindex, currentmaxindex;
  bool indexrange_defined;
  const void *transformfunc_data;
  bool writable;
};

static size_t gt_Sfxmappedrange_size_entire(const GtSfxmappedrange
                                              *sfxmappedrange)
{
  gt_assert(sfxmappedrange != NULL);
  return sfxmappedrange->sizeofunit * sfxmappedrange->numofunits;
}

void *gt_Sfxmappedrange_map_entire(GtSfxmappedrange *sfxmappedrange,
                                   GtError *err)
{
  size_t mappedsize;

  gt_assert(sfxmappedrange != NULL);
  sfxmappedrange->entire = gt_fa_mmap_read(gt_str_get(sfxmappedrange->filename),
                                           &mappedsize,err);
  if (sfxmappedrange->entire == NULL)
  {
    return NULL;
  }
  if (mappedsize != gt_Sfxmappedrange_size_entire(sfxmappedrange))
  {
    gt_error_set(err,"map file %s: mapped size = %lu != %lu = "
                     "expected size",
                      gt_str_get(sfxmappedrange->filename),
                      (unsigned long) mappedsize,
                      (unsigned long)
                                 gt_Sfxmappedrange_size_entire(sfxmappedrange));
    gt_fa_xmunmap(sfxmappedrange->entire);
    sfxmappedrange->entire = NULL;
    return NULL;
  }
  return sfxmappedrange->entire;
}

GtSfxmappedrange *gt_Sfxmappedrange_new(const char *tablename,
                                        unsigned long numofentries,
                                        GtSfxmappedrangetype type,
                                        GtSfxmappedrangetransformfunc
                                          transformfunc,
                                        const void *transformfunc_data)
{
  GtSfxmappedrange *sfxmappedrange;

  sfxmappedrange = gt_malloc(sizeof (*sfxmappedrange));
  sfxmappedrange->ptr = NULL;
  sfxmappedrange->pagesize = (unsigned long) sysconf((int) _SC_PAGESIZE);
  sfxmappedrange->usedptrptr = NULL;
  sfxmappedrange->filename = NULL;
  sfxmappedrange->writable = false;
  sfxmappedrange->entire = NULL;
  sfxmappedrange->transformfunc = transformfunc;
  sfxmappedrange->transformfunc_data = transformfunc_data;
  sfxmappedrange->type = type;
  sfxmappedrange->tablename = gt_str_new_cstr(tablename);
  sfxmappedrange->currentminindex = sfxmappedrange->currentmaxindex = 0;
  sfxmappedrange->indexrange_defined = false;
  switch (type)
  {
    case GtSfxGtBitsequence:
      sfxmappedrange->sizeofunit = sizeof (GtBitsequence);
      sfxmappedrange->numofunits = GT_NUMOFINTSFORBITS(numofentries);
      break;
    case GtSfxuint32_t:
      sfxmappedrange->sizeofunit = sizeof (uint32_t);
      sfxmappedrange->numofunits = (size_t) numofentries;
      break;
    case GtSfxunsignedlong:
      sfxmappedrange->sizeofunit = sizeof (unsigned long);
      sfxmappedrange->numofunits = (size_t) numofentries;
      break;
    default:
      gt_assert(false);
      break;
  }
  return sfxmappedrange;
}

void gt_Sfxmappedrange_storetmp(GtSfxmappedrange *sfxmappedrange,
                                void **usedptrptr,
                                bool writable)
{
  FILE *outfp;

  gt_assert(sfxmappedrange != NULL);
  sfxmappedrange->ptr = NULL;
  sfxmappedrange->usedptrptr = usedptrptr;
  sfxmappedrange->filename = gt_str_new();
  sfxmappedrange->writable = writable;
  outfp = gt_xtmpfp(sfxmappedrange->filename);
  gt_assert(outfp != NULL);
  gt_log_log("write %s to file %s (%lu units of %lu bytes)",
             gt_str_get(sfxmappedrange->tablename),
             gt_str_get(sfxmappedrange->filename),
             (unsigned long) sfxmappedrange->numofunits,
             (unsigned long) sfxmappedrange->sizeofunit);
  gt_xfwrite(*sfxmappedrange->usedptrptr,sfxmappedrange->sizeofunit,
             sfxmappedrange->numofunits,outfp);
  gt_fa_fclose(outfp);
  gt_free(*sfxmappedrange->usedptrptr);
  *sfxmappedrange->usedptrptr = NULL;
}

static unsigned long gt_Sfxmappedrange_size_mapped(const GtSfxmappedrange
                                                      *sfxmappedrange,
                                                   unsigned long minindex,
                                                   unsigned long maxindex)
{
  GtMappedrange lbrange;

  gt_assert(sfxmappedrange != NULL);
  if (sfxmappedrange->transformfunc != NULL)
  {
    minindex = sfxmappedrange->transformfunc(minindex,
                                            sfxmappedrange->transformfunc_data);
    maxindex = sfxmappedrange->transformfunc(maxindex,
                                            sfxmappedrange->transformfunc_data);
  }
  gt_Sfxmapped_offset_end(&lbrange,
                          sfxmappedrange->sizeofunit,
                          sfxmappedrange->pagesize,
                          minindex,
                          maxindex);
  return lbrange.mapend - lbrange.mapoffset + 1;
}

void *gt_Sfxmappedrange_map(GtSfxmappedrange *sfxmappedrange,
                            unsigned int part,
                            unsigned long minindex,
                            unsigned long maxindex)
{
  GtMappedrange lbrange;
  unsigned long unitoffset;
  size_t sizeoftable;

  gt_assert(sfxmappedrange != NULL);
  if (sfxmappedrange->ptr != NULL)
  {
    gt_fa_xmunmap(sfxmappedrange->ptr);
  }
  if (sfxmappedrange->transformfunc != NULL)
  {
    minindex = sfxmappedrange->transformfunc(minindex,
                                            sfxmappedrange->transformfunc_data);
    maxindex = sfxmappedrange->transformfunc(maxindex,
                                            sfxmappedrange->transformfunc_data);
  }
  gt_Sfxmapped_offset_end(&lbrange,
                          sfxmappedrange->sizeofunit,
                          sfxmappedrange->pagesize,
                          minindex,
                          maxindex);
  sizeoftable = gt_Sfxmappedrange_size_entire(sfxmappedrange);
  gt_log_log("part %u: mapped %s from %lu to %lu for %s (%.1f%% of all)",
             part,gt_str_get(sfxmappedrange->tablename),lbrange.mapoffset,
             lbrange.mapend,
             sfxmappedrange->writable ? "writing" : "reading",
             (lbrange.mapend - lbrange.mapoffset + 1
               >= (unsigned long) sizeoftable)
                  ? 100.0
                  : 100.0 * (lbrange.mapend - lbrange.mapoffset + 1)/
                             sizeoftable);
  gt_assert(lbrange.mapoffset <= lbrange.mapend);
  gt_assert(lbrange.mapoffset <= minindex * sfxmappedrange->sizeofunit);
  gt_assert(maxindex * sfxmappedrange->sizeofunit <= lbrange.mapend);
  gt_assert(lbrange.mapoffset % sfxmappedrange->pagesize == 0);
  if (sfxmappedrange->writable)
  {
    sfxmappedrange->ptr
      = gt_fa_xmmap_write_range (gt_str_get(sfxmappedrange->filename),
                                 (size_t) (lbrange.mapend-lbrange.mapoffset+1),
                                 (size_t) lbrange.mapoffset);
  } else
  {
    sfxmappedrange->ptr
      = gt_fa_xmmap_read_range (gt_str_get(sfxmappedrange->filename),
                                (size_t) (lbrange.mapend-lbrange.mapoffset+1),
                                (size_t) lbrange.mapoffset);
  }
  sfxmappedrange->indexrange_defined = true;
  sfxmappedrange->currentmaxindex = maxindex;
  sfxmappedrange->currentminindex = minindex;
  unitoffset = lbrange.mapoffset / sfxmappedrange->sizeofunit;
  switch (sfxmappedrange->type)
  {
    case GtSfxGtBitsequence:
      return ((GtBitsequence *) sfxmappedrange->ptr) - unitoffset;
    case GtSfxuint32_t:
      return ((uint32_t *) sfxmappedrange->ptr) - unitoffset;
    case GtSfxunsignedlong:
      return ((unsigned long *) sfxmappedrange->ptr) - unitoffset;
    default:
      gt_assert(false);
      break;
  }
  return NULL;
}

void gt_Sfxmappedrange_checkindex(const GtSfxmappedrange *sfxmappedrange,
                                  unsigned long idx)
{
  if (sfxmappedrange->indexrange_defined)
  {
    gt_assert(sfxmappedrange->currentminindex <= idx);
    gt_assert(idx <= sfxmappedrange->currentminindex);
  }
}

void gt_Sfxmappedrange_delete(GtSfxmappedrange *sfxmappedrange)
{
  if (sfxmappedrange == NULL)
  {
    return;
  }
  gt_log_log("delete table %s",gt_str_get(sfxmappedrange->tablename));
  gt_fa_xmunmap(sfxmappedrange->ptr);
  sfxmappedrange->ptr = NULL;
  gt_fa_xmunmap(sfxmappedrange->entire);
  sfxmappedrange->entire = NULL;
  if (sfxmappedrange->usedptrptr != NULL)
  {
    *sfxmappedrange->usedptrptr = NULL;
  }
  if (sfxmappedrange->filename != NULL)
  {
    gt_log_log("remove \"%s\"",gt_str_get(sfxmappedrange->filename));
    gt_xunlink(gt_str_get(sfxmappedrange->filename));
  }
  gt_str_delete(sfxmappedrange->tablename);
  gt_str_delete(sfxmappedrange->filename);
  gt_free(sfxmappedrange);
}

struct GtSfxmappedrangelist
{
  GtSfxmappedrange **arr;
  unsigned long nextfree, allocated;
};

GtSfxmappedrangelist *gt_Sfxmappedrangelist_new(void)
{
  GtSfxmappedrangelist *sfxmrlist = gt_malloc(sizeof (*sfxmrlist));

  sfxmrlist->arr = NULL;
  sfxmrlist->nextfree = sfxmrlist->allocated = 0;
  return sfxmrlist;
}

void gt_Sfxmappedrangelist_add(GtSfxmappedrangelist *sfxmrlist,
                               GtSfxmappedrange *sfxmappedrange)
{
  gt_assert(sfxmrlist != NULL);

  if (sfxmrlist->nextfree >= sfxmrlist->allocated)
  {
    sfxmrlist->allocated += 4UL;
    sfxmrlist->arr = gt_realloc(sfxmrlist->arr,sizeof (*sfxmrlist->arr) *
                                               sfxmrlist->allocated);
  }
  sfxmrlist->arr[sfxmrlist->nextfree++] = sfxmappedrange;
}

unsigned long gt_Sfxmappedrangelist_size_mapped(
                                         const GtSfxmappedrangelist *sfxmrlist,
                                         unsigned long minindex,
                                         unsigned long maxindex)
{
  unsigned long idx, sumsize = 0;

  for (idx = 0; idx < sfxmrlist->nextfree; idx++)
  {
    GtSfxmappedrange *sfxmappedrange = sfxmrlist->arr[idx];
    if (sfxmappedrange != NULL)
    {
      /*printf("size_mapped %s %lu %lu => %lu\n",
                  gt_str_get(sfxmappedrange->tablename),
                  minindex,maxindex,
                  gt_Sfxmappedrange_size_mapped(sfxmappedrange,minindex,
                                                maxindex));*/
      sumsize += gt_Sfxmappedrange_size_mapped(sfxmappedrange,minindex,
                                               maxindex);
    }
  }
  return sumsize;
}

unsigned long gt_Sfxmappedrangelist_size_entire(
                                         const GtSfxmappedrangelist *sfxmrlist)
{
  unsigned long idx, sumsize = 0;

  for (idx = 0; idx < sfxmrlist->nextfree; idx++)
  {
    GtSfxmappedrange *sfxmappedrange = sfxmrlist->arr[idx];
    if (sfxmappedrange != NULL)
    {
      sumsize += gt_Sfxmappedrange_size_entire(sfxmappedrange);
    }
  }
  return sumsize;
}

void gt_Sfxmappedrangelist_delete(GtSfxmappedrangelist *sfxmrlist)
{
  if (sfxmrlist != NULL)
  {
    gt_free(sfxmrlist->arr);
    gt_free(sfxmrlist);
  }
}
