## JPEG XL Image Loader for imlib2 ##
This is a loader for imlib2 that adds support for reading and writing [JPEG XL](https://jpeg.org/jpegxl/index.html) files.  This lets you view them using [feh](https://feh.finalrewind.org/), for example.  It relies on [libjxl](https://gitlab.com/wg1/jpeg-xl) for decoding the images.  libjxl is under heavy development, so breaking API changes are likely.

All JPEG XL files are supported, with the following limitations:
* All images are internally converted to ARGB with 8 bits per sample - this is a limitation of imlib2.
* Embedded colorspaces are ignored - everything is treated as sRGB - this is a limitation of imlib2(?).  (Possible future development: convert pixels to sRGB if we detect a different colorspace in the input.)
* For animated JXLs, only the first frame is decoded.


### Build ###
The loader has been built and tested on Linux (Kubuntu x86_64, Arch x86_64) using gcc and clang.  It only comes with a dumb makefile, as I'm not clever enough to create a configure script or use cmake.  However, the source was written to be portable and the build is fairly trivial, so it should be easy to get it working wherever you can install imlib2 and a C99 compiler.

#### Build Dependencies ####
* make.
* [imlib2](https://docs.enlightenment.org/api/imlib2/html/) with development headers (libimlib2-dev for Debian and similar).
* [libjxl](https://gitlab.com/wg1/jpeg-xl) with development headers.

Once those are installed...
```
git clone https://github.com/alistair7/imlib2-jxl.git
cd imlib2-jxl
make
```
This should produce jxl.so.

### Install ###
You need to install jxl.so to a location where imlib2 can find it.  On 64-bit Kubuntu this is /usr/lib/x86_64-linux-gnu/imlib2/loaders.  On Arch this is /usr/lib/imlib2/loaders.  `sudo make install` attempts to find the right location via pkg-config and copy the library there.  If that fails, you'll have to copy it to the right location yourself.

#### feh ####
As of version 3.6, feh verifies each file's magic bytes before passing it to imlib2 ([#505](https://github.com/derf/feh/issues/505)).  JXL files aren't recognised, so you will get an error similar to
```
feh WARNING: asdf.jxl - Does not look like an image (magic bytes missing)
```
To skip this check, set `FEH_SKIP_MAGIC=1` in feh's environment, and it will start working.

### Copying ###
The two header files were taken from [gawen947/imlib2-webp](https://github.com/gawen947/imlib2-webp) and are distributed according to their embedded license.  Everything else was written by me and is available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE).

### Rant ###
Writing a loader for imlib2 is harder than it should be.
Not only is there apparently _zero_ documentation on how to do so, imlib2's public API doesn't even expose the types you need to implement one.
So the options appear to be:
a) fork the entire imlib2 repo and integrate the loader into the build;
b) pick out all the important declarations and data structures, copy them into your project, and hope they don't go out of date too quickly;
c) same as option b, but someone has already hacked together the appropriate headers for you :).

I've attempted to document the callback functions in my JXL loader and describe to the best of my understanding what they're supposed to do.
