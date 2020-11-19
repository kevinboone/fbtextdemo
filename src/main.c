/*===========================================================================

  fbtextdemo

  main.c 

  This program demonstrates how to use libfreetype to convert characters
  into bitmaps, and then write them to a Linux framebuffer.

  It allows text strings to be placed at specific locations, with
  specified size, using a specific TTF font file.

  Note: this program only ASCII or ISO-88591-1 input at present.
  It only really works with a black screen background -- there is no 
  logic for merging the anti-aliasing greyscale data with the background.

  Copyright (c)2020 Kevin Boone, GPL 3.0

  =========================================================================*/
#include <stdio.h>
#include <assert.h>
#include <freetype2/ft2build.h>
#include <freetype/freetype.h>
#include <getopt.h>
#include "defs.h"
#include "log.h"
#include "framebuffer.h"

#define FBDEV "/dev/fb0"

/*===========================================================================

  init_ft 

  Initialze the FreeType library and load a specific .ttf file.
  Note that the various FT_xxx datatypes are _pointers_ to data structures,
  although this is not transparent. So an "FT_Face *" is actually 
  a pointer to where a pointer is stored by the caller. The parameter
  'face' is written with a reference to the specific font face, and
  'ft' with a reference to the library. The only place where ft will
  be used again is when tidying up; 'face' will be used by almost 
  every other call that manipulates glyphs.

  This function returns TRUE on success. If it fails, *error is written
  with an error message, that the caller should eventually free.

  =========================================================================*/
BOOL init_ft (const char *ttf_file, FT_Face *face, FT_Library *ft, 
               int req_size, char **error)
  {
  LOG_IN
  BOOL ret = FALSE;
  log_debug ("Requested glyph size is %d px", req_size);
  if (FT_Init_FreeType (ft) == 0) 
    {
    log_info ("Initialized FreeType");
    if (FT_New_Face(*ft, ttf_file, 0, face) == 0)
      {
      log_info ("Loaded TTF file");
      // Note -- req_size is a request, not an instruction
      if (FT_Set_Pixel_Sizes(*face, 0, req_size) == 0)
        {
        log_info ("Set pixel size");
        ret = TRUE;
        }
      else
        {
        log_error ("Can't set font size to %d", req_size);
        if (error)
          *error = strdup ("Can't set font size");
        }
      }
    else
      {
      log_error ("Can't load TTF file %s", ttf_file);
      if (error)
        *error = strdup ("Can't load TTF file");
      }
    }
  else
    {
    log_error ("Can't initialize FreeType library"); 
    if (error)
      *error = strdup ("Can't init freetype library");
    }

  LOG_OUT
  return ret;
  }

/*===========================================================================

  done_ft 

  Clean up after we've finished wih the FreeType library

  =========================================================================*/
void done_ft (FT_Library ft)
  {
  FT_Done_FreeType (ft);
  }

/*===========================================================================

  face_get_line_spacing

  Get the nominal line spacing, that is, the distance between glyph 
  baselines for vertically-adjacent rows of text. This is "nominal" because,
  in "real" typesetting, we'd need to add extra room for accents, etc.

  =========================================================================*/
int face_get_line_spacing (FT_Face face)
  {
  return face->size->metrics.height / 64;
  // There are other possibilities the give subtly different results:
  // return (face->bbox.yMax - face->bbox.yMin)  / 64;
  // return face->height / 64;
  }


/*===========================================================================

  face_draw_char_on_fb

  Draw a specific character, at a specific location, direct to the 
  framebuffer. The Y coordinate is the left-hand edge of the character.
  The Y coordinate is the top of the bounding box that contains all
  glyphs in the specific face. That is, (X,Y) are the top-left corner
  of where the largest glyph in the face would need to be drawn.
  In practice, most glyphs will be drawn a little below ths point, to
  make the baselines align. 

  The X coordinate is expressed as a pointer so it can be incremented, 
  ready for the next draw on the same line.

  =========================================================================*/
void face_draw_char_on_fb (FT_Face face, FrameBuffer *fb, 
      int c, int *x, int y)
  {
  // Note that TT fonts have no built-in padding. 
  // That is, first,
  //  the top row of the bitmap is the top row of pixels to 
  //  draw. These rows usually won't be at the face bounding box. We need to
  //  work out the overall height of the character cell, and
  //  offset the drawing vertically by that amount. 
  //
  // Similar, there is no left padding. The first pixel in each row will not
  //  be drawn at the left margin of the bounding box, but in the centre of
  //  the screen width that will be occupied by the glyph.
  //
  //  We need to calculate the x and y offsets of the glyph, but we can't do
  //  this until we've loaded the glyph, because metrics
  //  won't be available.

  // Note that, by default, TT metrics are in 64'ths of a pixel, hence
  //  all the divide-by-64 operations below.

  // Get a FreeType glyph index for the character. If there is no
  //  glyph in the face for the character, this function returns
  //  zero. We should really check for this, and substitute a default
  //  glyph. Naturally, the TTF font chosen must contain glyphs for
  //  all the characters to be displayed. 
  FT_UInt gi = FT_Get_Char_Index (face, c);

  // Loading the glyph makes metrics data available
  FT_Load_Glyph (face, gi, FT_LOAD_DEFAULT);

  // Now we have the metrics, let's work out the x and y offset
  //  of the glyph from the specified x and y. Because there is
  //  no padding, we can't just draw the bitmap so that it's
  //  TL corner is at (x,y) -- we must insert the "missing" 
  //  padding by aligning the bitmap in the space available.

  // bbox.yMax is the height of a bounding box that will enclose
  //  any glyph in the face, starting from the glyph baseline.
  int bbox_ymax = face->bbox.yMax / 64;
  // horiBearingX is the height of the top of the glyph from
  //   the baseline. So we work out the y offset -- the distance
  //   we must push down the glyph from the top of the bounding
  //   box -- from the height and the Y bearing.
  int y_off = bbox_ymax - face->glyph->metrics.horiBearingY / 64;

  // glyph_width is the pixel width of this specific glyph
  int glyph_width = face->glyph->metrics.width / 64;
  // Advance is the amount of x spacing, in pixels, allocated
  //   to this glyph
  int advance = face->glyph->metrics.horiAdvance / 64;
  // Work out where to draw the left-most row of pixels --
  //   the x offset -- by halving the space between the 
  //   glyph width and the advance
  int x_off = (advance - glyph_width) / 2;

  // So now we have (x_off,y_off), the location at which to
  //   start drawing the glyph bitmap.

  // Rendering a loaded glyph creates the bitmap
  FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

  // Write out the glyph row-by-row using framebuffer_set_pixel
  // Note that the glyph can contain horizontal padding. We need
  //  to take this into account when working out where the pixels
  //  are in memory, but we don't actually need to "draw" these
  //  empty pixels. bitmap.width is the number of pixels that actually
  //  contain values; bitmap.pitch is the spacing between bitmap
  //  rows in memory.
  for (int i = 0; i < (int)face->glyph->bitmap.rows; i++)
    {
    // Row offset is the distance from the top of the framebuffer
    //  of this particular row of pixels in the glyph.
    int row_offset = y + i + y_off;
    for (int j = 0; j < (int)face->glyph->bitmap.width; j++)
      {
      unsigned char p =
        face->glyph->bitmap.buffer [i * face->glyph->bitmap.pitch + j];
      
      // Working out the Y position is a little fiddly. horiBearingY 
      //  is how far the glyph extends about the baseline. We push
      //  the bitmap down by the height of the bounding box, and then
      //  back up by this "bearing" value. 
      if (p)
        framebuffer_set_pixel (fb, *x + j + x_off, row_offset, p, p, p);
      }
    }
  // horiAdvance is the nominal X spacing between displayed glyphs. 
  *x += advance;
  }

/*===========================================================================

  face_draw_string_on_fb

  draw a string of UTF32 characters (null-terminated), advancing each
  character by enough to create reasonable horizontal spacing. The
  X coordinate is expressed as a pointer so it can be incremented, 
  ready for the next draw on the same line.

  =========================================================================*/
void face_draw_string_on_fb (FT_Face face, FrameBuffer *fb, const UTF32 *s, 
       int *x, int y)
  {
  while (*s)
    {
    face_draw_char_on_fb (face, fb, *s, x, y);
    s++;
    }
  }

/*===========================================================================

  face_get_char_extent

  =========================================================================*/
void face_get_char_extent (const FT_Face face, int c, int *x, int *y)
  {
  // Note that, by default, TT metrics are in 64'ths of a pixel, hence
  //  all the divide-by-64 operations below.

  // Get a FreeType glyph index for the character. If there is no
  //  glyph in the face for the character, this function returns
  //  zero. We should really check for this, and substitute a default
  //  glyph. Naturally, the TTF font chosen must contain glyphs for
  //  all the characters to be displayed. 
  FT_UInt gi = FT_Get_Char_Index(face, c);

  // Loading the glyph makes metrics data available
  FT_Load_Glyph (face, gi, FT_LOAD_NO_BITMAP);

  *y = face_get_line_spacing (face);
  *x = face->glyph->metrics.horiAdvance / 64;
  }

/*===========================================================================

  face_get_string_extent

  UTF32 characters (null-terminated), 

  =========================================================================*/
void face_get_string_extent (const FT_Face face, const UTF32 *s, 
      int *x, int *y)
  {
  *x = 0;
  int y_extent = 0;
  while (*s)
    {
    int x_extent;
    face_get_char_extent (face, *s, &x_extent, &y_extent);
    *x += x_extent;
    s++;
    }
  *y = y_extent;
  }

/*===========================================================================

  utf8_to_utf32 

  Convert an 8-bit character string to a 32-bit character string; both
  are null-terminated.

  If this weren't just a demo, we'd have a real character set 
    conversion here. It's not that difficult, but it's not really what
    this demonstration is for. For now, just pad the 8-bit characters
    to 32-bit.

  =========================================================================*/
UTF32 *utf8_to_utf32 (const UTF8 *word)
  {
  assert (word != NULL);
  int l = strlen ((char *)word);
  UTF32 *ret = malloc ((l + 1) * sizeof (UTF32));
  for (int i = 0; i < l; i++)
    {
    ret[i] = (UTF32) word[i];
    }
  ret[l] = 0;
  return ret;
  }

/*===========================================================================

  usage

  Show a usage message

  =========================================================================*/
void usage (const char *argv0)
  {
  fprintf (stderr, "Usage %s [options] font_file word1 word2....\n", argv0);
  fprintf (stderr, "font_file is any TTF font file.\n");
  fprintf (stderr, "All positions and sizes are in screen pixels.\n");
  fprintf (stderr, "  -c,--clear             clear screen before writing\n");
  fprintf (stderr, "  -d,--dev=device        framebuffer device (/dev/fb0)\n");
  fprintf (stderr, "  -f,--font-size=N       font height in pixels (20)\n");
  fprintf (stderr, "  -l,--log-level=[0..4]  log verbosity (0) \n");
  fprintf (stderr, "  -h,--height=N          height of bounding box (500)\n");
  fprintf (stderr, "  -v,--version           show version\n");
  fprintf (stderr, "  -w,--width=N           width of bounding box (500)\n");
  fprintf (stderr, "  -x=N                   initial X coordinate (5)\n");
  fprintf (stderr, "  -y=N                   initial Y coordinate (5)\n");
  }

/*===========================================================================

  main

  =========================================================================*/
int main (int argc, char **argv)
  {
  // A string representating a single space in UTF32 format
  static const UTF32 utf32_space[2] = {' ', 0};

  // Variables set from the command line

  int init_x = 5;
  int init_y = 5;
  int width = 500;
  int height = 500;
  int font_size = 20;
  BOOL show_usage = FALSE;
  BOOL show_version = FALSE;
  BOOL clear = FALSE;
  char *fbdev = strdup (FBDEV);
  int log_level = LOG_ERROR;

  // Command line option table

  static struct option long_options[] =
    {
      {"help", no_argument, NULL, '?'},
      {"clear", no_argument, NULL, 'c'},
      {"version", no_argument, NULL, 'v'},
      {"log-level", required_argument, NULL, 'l'},
      {"dev", required_argument, NULL, 'd'},
      {"font-size", required_argument, NULL, 'f'},
      {"x", required_argument, NULL, 'x'},
      {"y", required_argument, NULL, 'y'},
      {"width", required_argument, NULL, 'w'},
      {"height", required_argument, NULL, 'h'},
      {0, 0, 0, 0}
    };

  // Parse the command line
  
  int opt;
 
  // ret will be set FALSE if something in the cmdline arguments means
  //  that the program should not continue
  BOOL ret = TRUE; 
   while (ret)
     {
     int option_index = 0;
     opt = getopt_long (argc, argv, "c?vl:f:x:y:w:h:d:",
     long_options, &option_index);

     if (opt == -1) break;

     switch (opt)
       {
       case 0:
         if (strcmp (long_options[option_index].name, "help") == 0)
           show_usage = TRUE; 
         else if (strcmp (long_options[option_index].name, "version") == 0)
           show_version = TRUE; 
         else if (strcmp (long_options[option_index].name, "clear") == 0)
           clear = TRUE; 
         else if (strcmp (long_options[option_index].name, "log-level") == 0)
           log_level = atoi (optarg);
         else if (strcmp (long_options[option_index].name, "width") == 0)
           width = atoi (optarg); 
         else if (strcmp (long_options[option_index].name, "height") == 0)
           height = atoi (optarg);
         else if (strcmp (long_options[option_index].name, "x") == 0)
           init_x = atoi (optarg); 
         else if (strcmp (long_options[option_index].name, "y") == 0)
           init_y = atoi (optarg); 
         else if (strcmp (long_options[option_index].name, "font-size") == 0)
           init_y = atoi (optarg); 
         else if (strcmp (long_options[option_index].name, "dev") == 0)
           { free (fbdev); fbdev = strdup (optarg); } 
         else
           exit (-1);
         break;
       case '?': 
         show_usage = TRUE; break;
       case 'v': 
         show_version = TRUE; break; 
       case 'c': 
         clear = TRUE; break; 
       case 'l':
           log_level = atoi (optarg); break;
       case 'w': 
           width = atoi (optarg); break;
       case 'h': 
           height = atoi (optarg); break;
       case 'f': 
           font_size = atoi (optarg); break;
       case 'x': 
           init_x = atoi (optarg); break; 
       case 'y': 
           init_y = atoi (optarg); break;
       case 'd': 
           free (fbdev); fbdev = strdup (optarg); break;
       default:
         ret = FALSE; 
       }
    }

  if (show_version)
    {
    printf ("%s: %s version %s\n", argv[0], NAME, VERSION);
    printf ("Copyright (c)2020 Kevin Boone\n");
    printf ("Distributed under the terms of the GPL v3.0\n");
    ret = FALSE;
    }

  if (show_usage)
    {
    usage (argv[0]);
    ret = FALSE;
    }

  log_set_level (log_level);

  if (ret)
    {
    // If we get here, we have some work to do.
    if (argc - optind >= 2)
      {
      char *ttf_file = argv[optind];
    
      char *error = NULL;

      FrameBuffer *fb = framebuffer_create (FBDEV);

      // Initializing the framebuffer may fail, particularly if the user
      //   doesn't have permissions.
      framebuffer_init (fb, &error);
      if (error == NULL)
	{
        log_debug ("FB initialized OK");
	// Initialize the FreeType library, and create a face of the specified
	//  size.
	FT_Face face;
	FT_Library ft;
	if (init_ft (ttf_file, &face, &ft, font_size, &error))
	  {
          log_debug ("Font face initialized OK");
	  if (clear)
	    framebuffer_clear (fb);

	  // Let's work out how wide a single space is in the current face, so we
	  //  don't have to keep recalculating it.
	  int space_y;
	  int space_x; // Pixel width of a space
	  face_get_string_extent (face, utf32_space, &space_x, &space_y); 

          log_debug ("Obtained a face whose space has height %d px", space_y);
	  log_debug ("Line spacing is %d px", face_get_line_spacing (face));

	  // x and y are the current coordinates of the top-left corner of
	  //  the bounding box of the text being written, relative to the
	  //  TL corner of the screen.
	  int x = init_x;
	  int y = init_y;

          log_debug ("Starting drawing at %d,%d", x, y);
	  int line_spacing = face_get_line_spacing (face);

	  // Loop around the remaining arguments to the program, printing
	  //  each word, followed by a space.
	  for (int i = optind + 1; i < argc; i++)
	    {
	    const char *word = argv[i];
            log_debug ("Next word is %s", word);

	    // The face_xxx text handling functions take UTF32 character strings
	    //  as input.
	    UTF32 *word32 = utf8_to_utf32 ((UTF8 *)word);
	    
	    // Get the extent of the bounding box of this word, to see 
	    //  if it will fit in the specified width.
	    int x_extent, y_extent;
	    face_get_string_extent (face, word32, &x_extent, &y_extent); 
	    int x_advance = x_extent + space_x;
            log_debug ("Word width is %d px; would advance X position by %d", x_extent, x_advance);

	    // If the text won't fit, move down to the next line
	    if (x + x_advance > width) 
	      {
              log_debug ("Text too large for bonuds -- move to next line");
	      x = init_x; 
	      y += line_spacing;
	      }
	    // If we're already below the specified height, don't write anything
	    if (y + line_spacing < init_y + height)
	      {
	      face_draw_string_on_fb (face, fb, word32, &x, y);
	      face_draw_string_on_fb (face, fb, utf32_space, &x, y);
	      }
	    free (word32);
	    }

	  done_ft (ft);
	  }
	else
	  {
	  fprintf (stderr, "%s\n", error);
	  free (error);
	  }
	framebuffer_deinit (fb);
	}
      else
	{
	fprintf (stderr, "Can't initialize framebuffer: %s\n", error);
	free (error);
	}

      framebuffer_destroy (fb);
      }
    else
      {
      usage (argv[0]);
      }
    }

  free (fbdev);
  return 0;
  }

