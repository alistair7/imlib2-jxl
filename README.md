## JPEG XL Image Loader for imlib2 ##
This is a loader for imlib2 that adds support for reading and writing [JPEG XL](https://jpeg.org/jpegxl/index.html) files.  This lets you view them using [feh](https://feh.finalrewind.org/), for example.  It relies on [libjxl](https://github.com/libjxl/libjxl) for encoding and decoding the images.

All JPEG XL files are supported, with the following limitations:
* All images are internally converted to ARGB with 8 bits per sample, and transformed to an sRGB colorspace - this is a limitation of imlib2.
* For animated JXLs, only the first frame is decoded.


### Build / Install ###
The loader has been built and tested on Linux (Kubuntu x86_64, Arch x86_64) using gcc and clang.

#### Dependencies ####
* [imlib2](https://docs.enlightenment.org/api/imlib2/html/) with development headers (libimlib2-dev for Debian and similar).
* [libjxl](https://github.com/libjxl/libjxl) with development headers.
* [liblcms2](https://github.com/mm2/Little-CMS) with development headers (liblcms2-dev for Debian and similar).

#### Arch Linux ####
There is a PKGBUILD available in AUR for easy installation of the latest release on Arch Linux:

```bash
git clone https://aur.archlinux.org/imlib2-jxl.git
cd imlib2-jxl
# (You should now review PKGBUILD before proceeding)
makepkg -s
sudo pacman -U imlib2-jxl-x.y.z.pkg.tar.zst
```

To install the cutting-edge git version, use the alternative package from https://aur.archlinux.org/imlib2-jxl-git.git.

#### Other Linuxes ####

Ensure the above dependencies are installed.  Download the imlib2-jxl source - either a release archive or the latest commit:
```
git clone https://github.com/alistair7/imlib2-jxl.git
```

Then simply running `make` should produce jxl.so:
```
cd imlib2-jxl
make
sudo make install
```

`make install` attempts to find the right location for the loader via pkg-config and copy jxl.so there. If this fails, you'll have to manually identify the
directory for imlib2 loaders and copy jxl.so there yourself.  On 64-bit Kubuntu this is /usr/lib/x86_64-linux-gnu/imlib2/loaders.
On Arch this is /usr/lib/imlib2/loaders.

### feh ###
If you are using a version of feh between 3.6 and 3.7.0, inclusive, JXL files will not be recognised (due to [#505](https://github.com/derf/feh/issues/505)), and you will get an error similar to
```
feh WARNING: asdf.jxl - Does not look like an image (magic bytes missing)
```
To avoid this, set `FEH_SKIP_MAGIC=1` in feh's environment, and it will start working.

This workaround is no longer necessary since feh version 3.7.1, which recognises JXL's signature.

### Debug Build ###
Use the `debug` target of the makefile to produce an unoptimized build that includes debugging symbols and prints information to stderr while it runs.
The resulting library will be called jxl-dbg.so.  Use `install-debug` to copy this to imlib2's loader directory.
```
make debug
sudo make install-debug
```
You can have the release and debug builds installed at the same time, but imlib2 will only use the first one it finds.

### Copying ###
All source code in this repository is available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-BSD-ab), © Alistair Barrow, with the following exception:
* [imlib2_common.h](https://github.com/alistair7/imlib2-jxl/blob/main/imlib2_common.h), [loader.h](https://github.com/alistair7/imlib2-jxl/blob/main/loader.h): available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-BSD-dh), © David Hauweele.

### Rant ###
Writing a loader for imlib2 is harder than it should be.
Not only is there apparently _zero_ documentation on how to do so, imlib2's public API doesn't even expose the types you need to implement one.
So the options appear to be:
a) fork the entire imlib2 repo and integrate the loader into the build;
b) pick out all the important declarations and data structures, copy them into your project, and hope they don't go out of date too quickly;
c) same as option b, but someone has already hacked together the appropriate headers for you :).

I've attempted to document the callback functions in my JXL loader and describe to the best of my understanding what they're supposed to do.
