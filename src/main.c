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
#include <stdbool.h>
#include <freetype2/ft2build.h>
#include <freetype/freetype.h>
#include <getopt.h>
#include "defs.h"
#include "log.h"
#include "framebuffer.h"

#define FBDEV "/dev/fb0"


/*===========================================================================
  WordGlyphs

  Structure to contain a word represented as its glyphs.
=========================================================================*/
typedef struct {
    UTF32 *word32;
    int x_extent;
    int y_extent;
} WordGlyphs;

/*===========================================================================
  GlyphsLine

  Structure to contain a sequence of glyphs belonging to a line.
=========================================================================*/
typedef struct {
    UTF32 **word32;
    int y_position;
    int word_count;
} GlyphsLine;

/*===========================================================================
  GlyphsBoundary

  Structure representing the boundary of a group of glyphs.
=========================================================================*/
typedef struct {
    int init_x;
    int init_y;
    int width;
    int height;
} GlyphsBoundary;

/*===========================================================================
  TextAlignmentType

  Alignment type for text to shown. TODO: 'Right' NOT implemented, yet!
=========================================================================*/
enum TextAlignmentType {
    Left,
    // Right,
    Center
};

typedef enum TextAlignmentType TextAlignmentType;

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

  next_utf8_glyph_length

  Gets the length of next glyph in a UTF-8 sequence.

  Returns -1 if the next glyph is not UTF-8.

  =========================================================================*/
int next_utf8_glyph_length(const UTF8 *word) {
    assert(word != NULL);

    // 1-byte glyph (0xxxxxxx).
    if ((*word & 0x80) == 0) {
        return 1;
    }
    // 2-byte glyph (110xxxxx).
    else if ((*word & 0xE0) == 0xC0) {
        return 2;
    }
    // 3-byte glyph (1110xxxx).
    else if ((*word & 0xF0) == 0xE0) {
        return 3;
    }
    // 4-byte glyph (11110xxx).
    else if ((*word & 0xF8) == 0xF0) {
        return 4;
    }

    // Invalid UTF-8 glyph.
    return -1;
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
UTF32 *utf8_to_utf32(const UTF8 *utf8_word)
{
    assert(utf8_word != NULL);

    // Compute the length of the resulting UTF-32 sequence.
    int word_length = 0;
    const UTF8 *utf8_word_ptr = utf8_word;

    while (*utf8_word_ptr) {
        int curr_glyph_length = next_utf8_glyph_length(utf8_word_ptr);
        utf8_word_ptr += curr_glyph_length;
        word_length++;
    }

    // Allocate memory for the UTF-32 sequence.
    UTF32 *utf32_word = (UTF32 *)malloc((word_length + 1) * sizeof(UTF32));
    UTF32 *utf32_word_ptr = utf32_word;
    utf8_word_ptr = utf8_word;

    // Convert UTF-8 to UTF-32 sequence.
    while (*utf8_word_ptr) {
        int curr_glyph_length = next_utf8_glyph_length(utf8_word_ptr);
        if (curr_glyph_length == 1) {
            *utf32_word_ptr = *utf8_word_ptr;
        }
        else if (curr_glyph_length == 2) {
            *utf32_word_ptr = ((*utf8_word_ptr & 0x1F) << 6);
            *utf32_word_ptr |= (*(utf8_word_ptr + 1) & 0x3F);
        }
        else if (curr_glyph_length == 3) {
            *utf32_word_ptr = ((*utf8_word_ptr & 0x0F) << 12);
            *utf32_word_ptr |= ((*(utf8_word_ptr + 1) & 0x3F) << 6);
            *utf32_word_ptr |= (*(utf8_word_ptr + 2) & 0x3F);
        }
        else if (curr_glyph_length == 4) {
            *utf32_word_ptr = ((*utf8_word_ptr & 0x07) << 18);
            *utf32_word_ptr |= ((*(utf8_word_ptr + 1) & 0x3F) << 12);
            *utf32_word_ptr |= ((*(utf8_word_ptr + 2) & 0x3F) << 6);
            *utf32_word_ptr |= (*(utf8_word_ptr + 3) & 0x3F);
        }
        utf32_word_ptr++;

        // Prepare for processing the next glyph.
        utf8_word_ptr += curr_glyph_length;
    }

    // Null-terminate the UTF-32 sequence.
    *utf32_word_ptr = 0;

    return utf32_word;
}

/*===========================================================================

  compute_glyphs_lines

  Computes a sequence of lines containing glyphs, taking into account the
  specified boundary, so:
  1. If the text overflows the boundary width, creates the required new lines.
  2. If the text overflows the boundary height, the overflowed text will not
     processed.

  =========================================================================*/
GlyphsLine* compute_glyph_lines(void* face, GlyphsBoundary glyphs_boundary, WordGlyphs *word_glyps, int word_count, int *out_line_count) {
    // Width and height of the 'space' character in the current font face.
    static const UTF32 utf32_space[2] = {' ', 0};
    int space_x, space_y;
	  face_get_string_extent(face, utf32_space, &space_x, &space_y);

    // Height between lines in the current font face.
    int line_spacing = face_get_line_spacing(face);

    // Initial position (x, y) where the text will be placed.
    int curr_x = glyphs_boundary.init_x;
    int curr_y = glyphs_boundary.init_y;

    // Allocate memory for storing words of an arbitrary line. Initially,
    // allocate based on the total number of words; later, reallocate based on
    // the actual number of words of the line.
    UTF32 **words_in_curr_line = (UTF32**) malloc(word_count * sizeof(UTF32*));
    if (!words_in_curr_line) {
        return NULL;
    }

    // Counter tracking the number of words of an arbitrary line.
    int curr_line_word_count = 0;

    // Counter tracking the total number of displayable words.
    int displayable_word_count = 0;

    // Allocate memory for storing computed glyph lines. Initially, allocate
    // based on the total number of words; later, reallocate based on the
    // actual number of processed lines.
    GlyphsLine *glyph_lines = (GlyphsLine*) malloc(word_count * sizeof(GlyphsLine));
    if (!glyph_lines) {
        free(words_in_curr_line);
        return NULL;
    }

    // Counter keeping track of the number of computed lines.
    int line_count = 0;

    // Flag indicating if the end of an arbitrary line has been reached.
    bool is_end_of_line = false;

    // Flag indicating if there the height is overflowed.
    bool height_is_overflowed = false;

    for (int curr_word_idx = 0; curr_word_idx < word_count; curr_word_idx++) {
        WordGlyphs curr_word_glyps = word_glyps[curr_word_idx];
        int curr_word_x_extent = curr_word_glyps.x_extent;
        int curr_word_x_advance = curr_word_x_extent + space_x;

        log_debug("Word width is %d px; would advance 'x' position by %d.", curr_word_x_extent, curr_word_x_advance);

        // If the current 'y' position exceeds the boundary height...
        if (curr_y + line_spacing >= glyphs_boundary.init_y + glyphs_boundary.height) {
            int remaining_word_count = word_count - displayable_word_count;
            log_warning("No more space within the y-boundary. Omitting %d words...", remaining_word_count);

            // Stop creating lines.
            height_is_overflowed = true;
            break;
        }

        // If the end of the line has been reached and at least one word has
        // been written, then create a new line.
        is_end_of_line = curr_x + curr_word_x_advance > glyphs_boundary.width;
        if (is_end_of_line && curr_line_word_count > 0) {
            log_debug("Text too large for bounds. Moving to the next line...");

            words_in_curr_line = (UTF32**) realloc(words_in_curr_line, curr_line_word_count * sizeof(UTF32*));

            // Add the sequence of glyphs as a current line.
            glyph_lines[line_count].word32 = words_in_curr_line;
            glyph_lines[line_count].word_count = curr_line_word_count;
            glyph_lines[line_count].y_position = curr_y;

            displayable_word_count += curr_line_word_count;
            curr_line_word_count = 0;
            line_count++;
            curr_x = glyphs_boundary.init_x;
            curr_y += line_spacing;

            // Allocate memory for storing words of an arbitrary line.
            int remaining_word_count = word_count - displayable_word_count;
            words_in_curr_line = (UTF32**) malloc((remaining_word_count) * sizeof(UTF32*));
        }
        words_in_curr_line[curr_line_word_count] = (curr_word_glyps.word32);
        curr_line_word_count++;
        curr_x += curr_word_x_advance;
    }

    // If boundary height wasn't overflowed, write the words of last line.
    if (!height_is_overflowed) {
        words_in_curr_line = (UTF32**) realloc(words_in_curr_line, curr_line_word_count * sizeof(UTF32*));
        glyph_lines[line_count].word32 = words_in_curr_line;
        glyph_lines[line_count].word_count = curr_line_word_count;
        glyph_lines[line_count].y_position = curr_y;
        line_count++;
    } else {
      free(words_in_curr_line);
    }

    // Resize to the actual number of renderable lines.
    glyph_lines = (GlyphsLine*) realloc(glyph_lines, line_count * sizeof(GlyphsLine));

    *out_line_count = line_count;

    return glyph_lines;
}

/*===========================================================================

  compute_init_x_to_center_line

  Computes the required 'init x' position to center a line within the x-axis of
  the glyphs boundary.

  =========================================================================*/
int compute_init_x_to_center_line(void *face, GlyphsLine *glyph_line, GlyphsBoundary glyphs_boundary) {
    // Width and height of the 'space' character in the current font face.
    // TODO: pass 'space_x' (as 'space_width'), avoiding this computation.
    static const UTF32 utf32_space[2] = {' ', 0};
    int space_x, space_y;
	  face_get_string_extent(face, utf32_space, &space_x, &space_y);

    int word_count = glyph_line->word_count;
    int line_width = 0;

    // Compute the width required for the space between glyphs.
    line_width += ((word_count - 1) * space_x);

    // Compute the width required for the glyphs.
    for (int curr_glyph_idx = 0; curr_glyph_idx < word_count; curr_glyph_idx++) {
        int word_x, word_y;

        // TODO: refactor 'glyph_line' to contain a sequence of 'WordGlyphs'
        // instead of 'UTF32', avoiding computation.
        face_get_string_extent(face, glyph_line->word32[curr_glyph_idx], &word_x, &word_y);
        line_width += word_x;
    }

    // Compute initial x-coordinate for centering.
    int middle_x = (glyphs_boundary.width / 2);
    int middle_line = (line_width / 2);
    int init_x = (middle_x - middle_line) + glyphs_boundary.init_x;

    return init_x;
}

/*===========================================================================

  draw_word_glyphs

  Draws a sequence of word glyphs onto the framebuffer. If the width of the
  text exceeds the specified bounds, creates a new line until all words have
  been written.
  =========================================================================*/
void draw_word_glyphs(void *face, void *fb, TextAlignmentType text_alignment, GlyphsBoundary glyphs_boundary, int word_count, WordGlyphs *word_glyps) {
    // Let's work out how wide a single space is in the current face, so we
	  // don't have to keep recalculating it.
	  int space_y;
	  int space_x; // Pixel width of a space

    // A string representating a single space in UTF32 format.
    static const UTF32 utf32_space[2] = {' ', 0};
	  face_get_string_extent(face, utf32_space, &space_x, &space_y);

    log_debug("Obtained a face whose space has height %d px", space_y);
	  log_debug("Line spacing is %d px", face_get_line_spacing (face));

    // Compute lines based on glyphs and the specified boundary.
    int line_count;
    GlyphsLine *glyph_lines = compute_glyph_lines(face, glyphs_boundary, word_glyps, word_count, &line_count);

    // Draw the computed lines.
    for (int curr_line_idx = 0; curr_line_idx < line_count; curr_line_idx++) {
        GlyphsLine *curr_glyph_line = &glyph_lines[curr_line_idx];

        int x = glyphs_boundary.init_x;
        if (text_alignment == Center) {
            x = compute_init_x_to_center_line(face, curr_glyph_line, glyphs_boundary);
            log_debug("Centering line in x=%d...", x);
        }
        int y = curr_glyph_line->y_position;

        log_debug("Drawing %d words in line %d at (%d,%d)...", curr_glyph_line->word_count, curr_line_idx + 1, x, y);

        for (int curr_word_idx = 0; curr_word_idx < curr_glyph_line->word_count; curr_word_idx++) {
            const UTF32 *curr_word = curr_glyph_line->word32[curr_word_idx];
            face_draw_string_on_fb(face, fb, curr_word, &x, y);
            face_draw_string_on_fb(face, fb, (UTF32 *)L" ", &x, y); // Single space in UTF32.
        }
    }
    free(glyph_lines);
}

/*===========================================================================
  parse_alignment

  Parses a string representing a type of an alignment. Then, returns the
  corresponding enum value. If cannot parse, returns 'Left' as default.
  =========================================================================*/
TextAlignmentType parse_alignment(char *string_type) {
    if (strcmp(string_type, "left") == 0) {
        return Left;
    } /*else if (strcmp(alignment_to_parse, "right") == 0) {
        return Right;
    }*/ else if (strcmp(string_type, "center") == 0) {
        return Center;
    }

    // Return default case: align to left.
    return Left;
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
  // Variables set from the command line

  int init_x = 5;
  int init_y = 5;
  int width = 500;
  int height = 500;
  int font_size = 20;
  TextAlignmentType text_alignment = Left;
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
      {"alignment", required_argument, NULL, 'a'},
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
     opt = getopt_long (argc, argv, "c?vl:f:x:y:w:h:d:a:",
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
         else if (strcmp (long_options[option_index].name, "alignment") == 0)
            text_alignment = parse_alignment(optarg);
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
       case 'a':
           text_alignment = parse_alignment(optarg); break;
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

    // Allocate memory for the array of word as glyphs.
    int word_count = argc - (optind + 1);
    WordGlyphs *word_glyps = (WordGlyphs *)malloc(word_count * sizeof(WordGlyphs));
    if (word_glyps == NULL) {
        fprintf(stderr, "Cannot allocate memory for the word glyps.\n");
        return 1;
    }

	  // Loop around the remaining arguments to the program, printing
	  //  each word, followed by a space.
	  int curr_word_idx = 0;
    for (int i = optind + 1; i < argc; i++) {
	    const char *word = argv[i];
            log_debug ("Next word is %s", word);

	    // The face_xxx text handling functions take UTF32 character strings
	    // as input.
	    UTF32 *word32 = utf8_to_utf32((UTF8 *)word);
	    
	    // Get the extent of the bounding box of this word, to see 
	    // if it will fit in the specified width.
	    int x_extent, y_extent;
	    face_get_string_extent(face, word32, &x_extent, &y_extent);

      // Save the word32 and extents in the structure
      word_glyps[curr_word_idx].word32 = word32;
      word_glyps[curr_word_idx].x_extent = x_extent;
      word_glyps[curr_word_idx].y_extent = y_extent;
      curr_word_idx++;
    }

    GlyphsBoundary glyphs_boundary;
    glyphs_boundary.init_x = init_x;
    glyphs_boundary.init_y = init_y;
    glyphs_boundary.width = width;
    glyphs_boundary.height = height;

    draw_word_glyphs(face, fb, text_alignment, glyphs_boundary, word_count, word_glyps);

    // Free memory.
    for (int curr_glyph_idx = 0; curr_glyph_idx < word_count; curr_glyph_idx++) {
      free(word_glyps[curr_glyph_idx].word32);
    }
    free(word_glyps);

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

