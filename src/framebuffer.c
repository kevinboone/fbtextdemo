/*============================================================================

  framebuffer.c

  Implementation of the "methods" defined in framebuffer.h. 

  Note that this implementation assumes a linear, 32-bpp framebuffer. 
  While this is a common framebuffer layout, it is by no means
  ubiquitous. The implementation allows for the fact that there can be
  "slop" at the end of a block of memory locations that doesn't map
  to pixels. However, it doesn't allow for non-sequential row ordering,
  or palette mapping, or any of that stuff.

  Note that all the methods in this implementation require that the
  user have write access to the framebuffer device in /dev. 

  Copyright (c)2020 Kevin Boone, GPL v3.0

============================================================================*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "defs.h" 
#include "log.h" 
#include "framebuffer.h" 

#define max(a, b) ((a) > (b) ? (a) : (b))

struct _FrameBuffer
  {
  int fd; // File descriptor
  int w; // Displayed width in pixels
  int h; // Displayer height in pixels
  int fb_data_size; // Total amount of mapped memory
  BYTE *fb_data; // Pointer to the mapped memory
  char *fbdev; // Original device name
  int fb_bytes; // Number of bytes per pixel -- must by 3 or 4
  int line_length; // Number of pixels in a line, as reported by the device
  int stride; // Bytes between vertically-adjacent rows of pixels
  int slop; // Amount of line_length that does not correspond to pixels.
  int rotate; // 0=none, 1=90 CW, 2=180, 3=90 CCW (same as fbcon/rotate)
  }; 


/*==========================================================================
  framebuffer_create
*==========================================================================*/
FrameBuffer *framebuffer_create (const char *fbdev)
  {
  LOG_IN
  FrameBuffer *self = malloc (sizeof (FrameBuffer));
  self->fbdev = strdup (fbdev);
  self->fd = -1;
  self->fb_data = NULL;
  self->fb_data_size = 0;
  self->rotate = 0;
  LOG_OUT 
  return self;
  }


/*==========================================================================
  framebuffer_init
*==========================================================================*/
BOOL framebuffer_init (FrameBuffer *self, char **error)
  {
  LOG_IN
  BOOL ret = FALSE;
  self->fd = open (self->fbdev, O_RDWR);
  if (self->fd >= 0)
    {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    ioctl (self->fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl (self->fd, FBIOGET_VSCREENINFO, &vinfo);

    log_debug ("fb_init: xres %d", vinfo.xres); 
    log_debug ("fb_init: yres %d", vinfo.yres); 
    log_debug ("fb_init: bpp %d",  vinfo.bits_per_pixel); 
    log_debug ("fb_init: line_length %d",  finfo.line_length); 

    self->line_length = finfo.line_length; 
    self->w = vinfo.xres;
    self->h = vinfo.yres;
    int fb_bpp = vinfo.bits_per_pixel;
    int fb_bytes = fb_bpp / 8;
    self->fb_bytes = fb_bytes;
    self->stride = max (self->line_length, self->w * self->fb_bytes);
    self->slop = self->stride - (self->w * self->fb_bytes);
    // Rows may be padded, so map stride * height, not width * height
    self->fb_data_size = self->stride * self->h;

    self->fb_data = mmap (0, self->fb_data_size, 
	     PROT_READ | PROT_WRITE, MAP_SHARED, self->fd, (off_t)0);

    if (self->fb_data != MAP_FAILED)
      ret = TRUE;
    else
      {
      self->fb_data = NULL;
      if (error)
        asprintf (error, "Can't map framebuffer: %s", strerror (errno));
      }
    }
  else
    {
    if (error)
      asprintf (error, "Can't open framebuffer: %s", strerror (errno));
    }
  LOG_OUT 
  return ret;
  }



/*==========================================================================
  framebuffer_clear
*==========================================================================*/
void framebuffer_clear (FrameBuffer *self)
  {
  memset (self->fb_data, 0, self->stride * self->h);
  }

/*==========================================================================
  framebuffer_deinit
*==========================================================================*/
void framebuffer_deinit (FrameBuffer *self)
  {
  LOG_IN
  if (self)
    {
    if (self->fb_data) 
      {
      munmap (self->fb_data, self->fb_data_size);
      self->fb_data = NULL;
      }
    if (self->fd != -1)
      {
      close (self->fd);
      self->fd = -1;
      }
    }
  LOG_OUT
  }


/*==========================================================================
  framebuffer_set_rotation
*==========================================================================*/
void framebuffer_set_rotation (FrameBuffer *self, int rotate)
  {
  self->rotate = rotate & 3;
  }

/*==========================================================================
  framebuffer_map
  Convert logical (rotated) coordinates to physical framebuffer
  coordinates. Returns FALSE if the point is off-screen.
*==========================================================================*/
static BOOL framebuffer_map (const FrameBuffer *self, int x, int y,
      int *px, int *py)
  {
  switch (self->rotate)
    {
    case 1: // 90 degrees clockwise
      *px = self->w - 1 - y; *py = x; break;
    case 2: // 180 degrees
      *px = self->w - 1 - x; *py = self->h - 1 - y; break;
    case 3: // 90 degrees counter-clockwise
      *px = y; *py = self->h - 1 - x; break;
    default:
      *px = x; *py = y;
    }
  return (*px >= 0 && *px < self->w && *py >= 0 && *py < self->h);
  }

/*==========================================================================
  framebuffer_set_pixel
*==========================================================================*/
void framebuffer_set_pixel (FrameBuffer *self, int x, int y, 
      BYTE r, BYTE g, BYTE b)
  {
  if (framebuffer_map (self, x, y, &x, &y))
    {
    int index32 = (y * self->w + x) * self->fb_bytes + y * self->slop;
    self->fb_data [index32++] = b;
    self->fb_data [index32++] = g;
    self->fb_data [index32++] = r;
    self->fb_data [index32] = 0;
    }
  }

/*==========================================================================
  framebuffer_destroy
*==========================================================================*/
void framebuffer_destroy (FrameBuffer *self)
  {
  LOG_IN
  framebuffer_deinit (self);
  if (self)
    {
    if (self->fbdev) free (self->fbdev);
    free (self); 
    }
  LOG_OUT
  }

/*==========================================================================
  framebuffer_get_width
*==========================================================================*/
int framebuffer_get_width (const FrameBuffer *self)
  {
  return (self->rotate & 1) ? self->h : self->w;
  }

/*==========================================================================
  framebuffer_get_height
*==========================================================================*/
int framebuffer_get_height (const FrameBuffer *self)
  {
  return (self->rotate & 1) ? self->w : self->h;
  }

/*==========================================================================
  framebuffer_get_pixel
*==========================================================================*/
void framebuffer_get_pixel (const FrameBuffer *self, 
                      int x, int y, BYTE *r, BYTE *g, BYTE *b)
  {
  if (framebuffer_map (self, x, y, &x, &y))
    {
    int index32 = (y * self->w + x) * self->fb_bytes + (y * self->slop);
    *b = self->fb_data [index32++];
    *g = self->fb_data [index32++];
    *r = self->fb_data [index32];
    }
  else
    {
    *r = 0;
    *g = 0;
    *b = 0;
    }
  }

/*==========================================================================
  framebuffer_get_data
*==========================================================================*/
BYTE *framebuffer_get_data (FrameBuffer *self)
  {
  return self->fb_data;
  }


