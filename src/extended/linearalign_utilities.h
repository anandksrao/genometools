/*
  Copyright (c) 2015 Annika <annika.seidel@studium.uni-hamburg.de>
  Copyright (c) 2015 Center for Bioinformatics, University of Hamburg

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

#ifndef LINEARALIGN_UTILITIES_H
#define LINEARALIGN_UTILITIES_H
#include "core/types_api.h"

/* file with all useful uitlities for different algorithms,
 * which work in linear space */

/* struct to organize all space allocated */
typedef struct LinspaceManagement LinspaceManagement;

LinspaceManagement* gt_linspaceManagement_new();

void gt_linspaceManagement_delete(LinspaceManagement *space);

/* checks if enough space is allocated */
void gt_linspaceManagement_check(LinspaceManagement *spacemanager,
                                 GtUword ulen, GtUword vlen,
                                 size_t valuesize,
                                 size_t rtabsize,
                                 size_t crosspointsize);

void *gt_linspaceManagement_get_valueTabspace(LinspaceManagement *space);

void *gt_linspaceManagement_get_rTabspace(LinspaceManagement *space);

void *gt_linspaceManagement_get_crosspointTabspace(LinspaceManagement *space);

GtUchar* sequence_to_lower_case(const GtUchar *seq, GtUword len);
inline GtWord add_safe_max(GtWord val1, GtWord val2);
inline GtWord add_safe_min(GtWord val1, GtWord val2);
inline GtUword add_safe_umax(GtUword val1, GtUword val2);
#endif
