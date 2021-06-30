## JPEG XL Image Loader for imlib2 ##
This is a loader for imlib2 that adds support for reading and writing [JPEG XL](https://jpeg.org/jpegxl/index.html) files.  This lets you view them using [feh](https://feh.finalrewind.org/), for example.  It relies on [libjxl](https://github.com/libjxl/libjxl) for encoding and decoding the images.

All JPEG XL files are supported, with the following limitations:
* All images are internally converted to ARGB with 8 bits per sample, in an sRGB colorspace - this is a limitation of imlib2.
* For animated JXLs, only the first frame is decoded.


### Build ###
The loader has been built and tested on Linux (Kubuntu x86_64, Arch x86_64) using gcc and clang.  It only comes with a dumb makefile, as I'm not clever enough to create a configure script or use cmake.  However, the source was written to be portable and the build is fairly trivial, so it should be easy to get it working wherever you can install imlib2 and a C99 compiler.

#### Build Dependencies ####
* make.
* [imlib2](https://docs.enlightenment.org/api/imlib2/html/) with development headers (libimlib2-dev for Debian and similar).
* [libjxl](https://github.com/libjxl/libjxl) with development headers.
* [liblcms2](https://github.com/mm2/Little-CMS) with development headers (liblcms2-dev for Debian and similar).

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

This workaround is no longer necessary in the latest git version of feh, which recognises JXL's signature.

### Debug Build ###
Use the `debug` target of the makefile to produce an unoptimized build that includes debugging symbols and prints information to stderr while it runs.
The resulting library will be called jxl-dbg.so.  Use `install-debug` to copy this to imlib2's loader directory.
```
make debug
sudo make install-debug
```
You can have the release and debug builds installed at the same time, but imlib2 will only use the first one it finds.

### Copying ###
All source code in this repository is available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-BSD-ab), © Alistair Barrow, with the following exceptions:
* [imlib2_common.h](https://github.com/alistair7/imlib2-jxl/blob/main/imlib2_common.h), [loader.h](https://github.com/alistair7/imlib2-jxl/blob/main/loader.h): available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-BSD-dh), © David Hauweele.
* [PKGBUILD](https://github.com/alistair7/imlib2-jxl/blob/main/PKGBUILD): available under [GPL3](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-GPL3).

### Rant ###
Writing a loader for imlib2 is harder than it should be.
Not only is there apparently _zero_ documentation on how to do so, imlib2's public API doesn't even expose the types you need to implement one.
So the options appear to be:
a) fork the entire imlib2 repo and integrate the loader into the build;
b) pick out all the important declarations and data structures, copy them into your project, and hope they don't go out of date too quickly;
c) same as option b, but someone has already hacked together the appropriate headers for you :).

I've attempted to document the callback functions in my JXL loader and describe to the best of my understanding what they're supposed to do.
