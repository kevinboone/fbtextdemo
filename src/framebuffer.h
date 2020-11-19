/*============================================================================

  framebuffer.h

  A "class" for doing primitive manipulations of a Linux framebuffer device.

  The usual sequence of operations is
  framebuffer_create
  framebuffer_init
  framebuffer_set_pixel (probably many times)
  framebuffer_deinit
  framebuffer_destroy

  Copyright (c)2020 Kevin Boone, GPL v3.0

============================================================================*/

#pragma once

#include "defs.h"

struct _FrameBuffer;
typedef struct _FrameBuffer FrameBuffer;

BEGIN_DECLS

/** Create a new Framebuffer object. This method always succeeds, and
    must always be followed eventually by a call to framebuffer_destroy(). */
FrameBuffer     *framebuffer_create (const char *fbdev);

/** Initialize the framebuffer device, get its properties, and map its
    data area into memory. This method can fail, usually for lack of
    permissions. If it succeeds, the caller must eventually call 
    framebuffer_deinit(). */
BOOL             framebuffer_init (FrameBuffer *self, char **error);

/** Tidy up the work done by framebuffer_init(). */
void             framebuffer_deinit (FrameBuffer *self);

/** Delete this object and free memory. Note that this method will not
    "closed" the framebuffer -- use _deinit() for this. */
void             framebuffer_destroy (FrameBuffer *self);

/** Set the specified pixel to the specified RGB colour values. 
    Note that repeated calls to this method are quite inefficient, as
    the pixel coordinates are converted to memory locations in every
    call. Still, these overheads are usually a small price to pay
    for encapsulating the gritty details of the framebuffer memory. */
void             framebuffer_set_pixel (FrameBuffer *self, int x,
                      int y, BYTE r, BYTE g, BYTE b);

/** Get the width of the framebuffer in pixels. The FB must be
    initialized first. */
int              framebuffer_get_width (const FrameBuffer *self);

/** Get the height of the framebuffer in pixels. The FB must be
    initialized first. */
int              framebuffer_get_height (const FrameBuffer *self);

/** Get the RGB colour values of a specific pixel. */
void             framebuffer_get_pixel (const FrameBuffer *self, 
                      int x, int y, BYTE *r, BYTE *g, BYTE *b);

/** Get a pointer to the data area. This might be useful for
    bulk manipulations, but the caller will need to know the structure
    of the framebuffer's memory to make much sense of it. */ 
BYTE            *framebuffer_get_data (FrameBuffer *self);

/** Set the whole framebuffer to black. */
void             framebuffer_clear (FrameBuffer *self);

END_DECLS

