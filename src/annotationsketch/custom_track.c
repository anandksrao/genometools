/*
  Copyright (c) 2008 Sascha Steinbiss <steinbiss@zbh.uni-hamburg.de>
  Copyright (c) 2008 Center for Bioinformatics, University of Hamburg

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

#include "annotationsketch/custom_track_rep.h"
#include "annotationsketch/custom_track.h"
#include "core/assert_api.h"
#include "core/class_alloc.h"
#include "core/ma.h"

struct GtCustomTrackMembers {
  unsigned int reference_count;
};

struct GtCustomTrackClass {
  size_t size;
  GtCustomTrackSketchFunc sketch;
  GtCustomTrackGetHeightFunc get_height;
  GtCustomTrackFreeFunc free;
};

const GtCustomTrackClass* gt_custom_track_class_new(size_t size,
                                          GtCustomTrackSketchFunc sketch,
                                          GtCustomTrackGetHeightFunc get_height,
                                          GtCustomTrackFreeFunc free)
{
  GtCustomTrackClass *c_class = gt_class_alloc(sizeof *c_class);
  c_class->size = size;
  c_class->sketch = sketch;
  c_class->get_height = get_height;
  c_class->free = free;
  return c_class;
}

GtCustomTrack* gt_custom_track_create(const GtCustomTrackClass *ctc)
{
  GtCustomTrack *ct;
  gt_assert(ctc && ctc->size);
  ct = gt_calloc(1, ctc->size);
  ct->c_class = ctc;
  ct->pvt = gt_calloc(1, sizeof (GtCustomTrackMembers));
  return ct;
}

GtCustomTrack* gt_custom_track_ref(GtCustomTrack *ct)
{
  gt_assert(ct);
  ct->pvt->reference_count++;
  return ct;
}

void gt_custom_track_delete(GtCustomTrack *ct)
{
  if (!ct) return;
  if (ct->pvt->reference_count) {
    ct->pvt->reference_count--;
    return;
  }
  gt_assert(ct->c_class);
  if (ct->c_class->free)
    ct->c_class->free(ct);
  gt_free(ct->pvt);
  gt_free(ct);
}

int gt_custom_track_sketch(GtCustomTrack *ct, GtGraphics *graphics,
                           unsigned int start_ypos, GtRange viewrange,
                           GtStyle *style)
{
  gt_assert(ct && ct->c_class && graphics && style);
  return ct->c_class->sketch(ct, graphics, start_ypos, viewrange, style);
}

unsigned long gt_custom_track_get_height(GtCustomTrack *ct)
{
  gt_assert(ct && ct->c_class);
  return ct->c_class->get_height(ct);
}

void* gt_custom_track_cast(const GtCustomTrackClass *ctc,
                           GtCustomTrack *ct)
{
  gt_assert(ctc && ct && ct->c_class == ctc);
  return ct;
}
