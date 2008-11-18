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

#ifndef CUSTOM_TRACK_H
#define CUSTOM_TRACK_H

typedef struct GtCustomTrackClass GtCustomTrackClass;

#include "annotationsketch/custom_track_api.h"
#include "annotationsketch/graphics.h"
#include "annotationsketch/style.h"
#include "core/range.h"

/* Call the drawing functionality for <ctrack>. It will be drawn to <graphics>.
   The values for <start_ypos>, <viewrange> and <style> reflect the settings
   of the current context (this is: the diagram the track was added to). */
int           gt_custom_track_sketch(GtCustomTrack *ctrack,
                                     GtGraphics *graphics,
                                     unsigned int start_ypos,
                                     GtRange viewrange,
                                     GtStyle *style);
/* Returns the height of the given <ctrack> in pixels/points. */
unsigned long gt_custom_track_get_height(GtCustomTrack *ctrack);

#endif
