# fbtextdemo

A simple command-line utility for Linux, for writing text nicely from
TTF fonts to the framebuffer. 

This utility demonstrates
how to use `libfreetype` directly, for rendering character glyphs
into memory, and how to transfer the rendered bitmap
pixel-by-pixel to the framebuffer.

The `libfreetype` library has built-in support for font anti-aliasing,
and using it produces much nicer output than the bitmapped fonts that
are widely used in applications that write to the framebuffer,
without much more effort.

## Prerequisites

To run the utility you'll need `libfreetype` and its dependencies --
these usually only amount to `libpng`. To build, you'll need the
`libfreetype` development headers. On desktop systems you can probably do
something like `apt-get install libfreetype6-dev`.

To run on a system that already has a graphical desktop, you'll
need to find a way to switch out X temporarily, and get to a
regular text console. On many desktops you can switch 
virtual terminals by pressing some combination of ctrl+alt+function
keys.

Graphical desktop don't usually draw on the framebuffer device,
although this is possible. Usually the traditional framebuffer
is an alternative display device. The framebuffer device is
usually `/dev/fb0`. The user will need to have write access
to this device in order to run `fbtextdemo`.

## Build and install

`fbtextdemo` is designed to be built using GCC, and it uses some
GCC-specific extensions. It may build on systems other than Linux,
but I've not tested this. 

You might need to modify the `Makefile` to indicate the location of
the `libfreetype` C headers. In many desktop systems you should be
able to get the location using `pkg-config --cflags libfreetype2`.

    $ make
    $ sudo make install

Most modern Linux systems that have a graphical desktop
have many TTF fonts installed. Try:

    $ find /usr/share -name "*.ttf"

These files are self-contained, and most of those provided by Linux
distributions are freely distributable.

## Usage

    fbtextdemo [options] font_file Any text you want to display...

`font_file` is any TTF font file (one is included in the repository).

`fbtextdemo` outputs its arguments, in the same way that `echo` does.

An example of usage is provided in the file `test.sh` in the source
code bundle.

The following command-line options are recognized.  
All positions and sizes are in screen pixels.

`-c,--clear`

Clear the framebuffer (to black) before drawing. Otherwise, text will
be written over the existing framebuffer contents.

`-d,--dev=device`

Specify the framebuffer device. Defaults to `/dev/fb0`.

`-f,--font-size=N`       

Request a font height in pixels (default 20). Note that this is only
a request -- there's no guarantee that the TTF file will be able to
provide a rendering that is an exact match for this height.

`-l,--log-level=[0..4]`

Set log verbosity, from 0 (fatal errors only) to 4 (huge volume of tracing) 
Default is 0.

`-h,--height=N`

Set the height of the bounding box in pixels. The utility will only
write whole lines of text -- if the text is too large it won't be
clipped: the non-fitting lines won't be displayed at all. Default
value is 500.

`-v,--version`

Show the version.

`-w,--width=N` 

Set the width of bounding box in pixels (default 500). Text will be
broken at spaces to fit into this width.

`-x=N`

X coordinate of upper-left corner of text output (default 5).

`-y=N`

Y coordinate of upper-left corner of text output (default 5).

`-a,--alignment=[left|center]`

Set the text alignment according to the x-axis bounds (`-x` and
`-w` options). The default alignment is to the left.

## Limitations

Although there might be some circumstances in which this utility is useful,
it's mostly provided as a demonstration of how to use `libfreetype`.

Most noticeably, this utility will only work with linear
24bpp or 32bpp framebuffers. Most Linux display devices are of this
type but (sorry) I'm aware that there are some that aren't. Unfortunately,
I don't have access to any Linux device that has any other kind of
framebuffer. If somebody wants to provide me with one for testing
purposes, be my guest. 

Although `fbtextdemo` can render onto the existing contents of the
framebuffer, it won't anti-alias properly if the background is anything
other than black. It wouldn't be difficult to merge the rendered 
glyphs with an existing background, rather than simply overwriting it,
 but so far this isn't implemented.

`fbtextdemo` will wrap text within the specified bounding rectangle.
If will prevent text overflowing the bounds in any direction. However,
it won't stop you specifying a bounding rectangle that is not 
on screen at all. 

The text can be aligned to the left or the center of the x-axis bounds;
however, alignment to the right is not yet implemented.

## Author and legal

`fbtextdemo` was written by Kevin Boone, and released under the terms
of the GNU Public Licence, v3.0. There is no warranty of any kind.

## More information

For details of operation, see:

http://kevinboone.me/fbtextdemo.html



