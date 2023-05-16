## JPEG XL Image Loader for imlib2 ##
This is a loader for imlib2 that adds support for reading and writing [JPEG XL](https://jpeg.org/jpegxl/index.html) files.
This lets you view them using [feh](https://feh.finalrewind.org/), for example.
It relies on [libjxl](https://github.com/libjxl/libjxl) for encoding and decoding the images.

All JPEG XL files are supported, with the following limitations:
* All images are internally converted to ARGB with 8 bits per sample, and transformed to an sRGB color profile - this is a limitation of imlib2.
* For animated JXLs, only the first frame is decoded.

## You probably don't need this loader ##
imlib2 now comes with its own JXL loader, so you might prefer to use that.

 - imlib2's loader can decode all frames of animated JXLs, while this one only decodes the first.
 - imlib2's loader uses less memory.
 - imlib2's loader is better behaved with respect to reporting progress and allowing decoding to be cancelled half way.

On the other hand,

 - This loader ensures the pixels fed back to the library are using a standard sRGB profile, which gives more consistent results.
   imlib2's loader ignores color profiles.

### Build / Install ###
The loader has been built and tested on Linux (Kubuntu x86_64, Arch x86_64) using gcc and clang.

Since imlib2 introduced its own JXL loader, installation of this alternative loader requires you to *remove, rename or replace* the
official one, since imlib2 always prefers to use the loader named `jxl.so` before considering any others.

#### Dependencies ####
- [imlib2](https://docs.enlightenment.org/api/imlib2/html/) with development headers (libimlib2-dev for Debian and similar).
    - Specifically, the build requires [`Imlib2_Loader.h`](https://git.enlightenment.org/old/legacy-imlib2/src/branch/master/src/lib/Imlib2_Loader.h).
      On Arch, this is installed comes with the `imlib2` package.
- [libjxl](https://github.com/libjxl/libjxl) with development headers.
- (Optional) [liblcms2](https://github.com/mm2/Little-CMS) with development headers (liblcms2-dev for Debian and similar).

#### Arch Linux ####
There is a PKGBUILD available in AUR for easy installation of the latest release on Arch Linux:

```bash
git clone https://aur.archlinux.org/imlib2-jxl.git
cd imlib2-jxl
# (You should now review PKGBUILD before proceeding)
makepkg -s
sudo pacman -U imlib2-jxl-x.y.z.pkg.tar.zst
```

**Note, this AUR package rudely overwrites the jxl.so distributed with the imlib2 package!**

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

**Note, make install overwrites the jxl.so distributed with the imlib2 package!**

#### Debug Build ####
Use the `debug` target of the makefile to produce an unoptimized build that includes debugging symbols and prints information to stderr while it runs.
The resulting library will be called jxl-dbg.so.  Use `install-debug` to copy this to imlib2's loader directory.
```
make debug
sudo make install-debug
```

You must remove jxl.so in order for imlib2 to see jxl-dbg.so.


#### Building without lcms2 ####
You can build this loader without lcms2 - this simply disables color management.
This requires editing 3 lines in `Makefile`:
- Remove `-DIMLIB2JXL_USE_LCMS` from `CPPFLAGS`.
- Remove `pkg-config lcms2 --cflags` from `SHARED_CFLAGS`.
- Remove `pkg-config lcms2 --libs` from `LDFLAGS`.


### feh ###
If you are using a version of feh between 3.6 and 3.7.0, inclusive, JXL files will not be recognised (due to [#505](https://github.com/derf/feh/issues/505)), and you will get an error similar to
```
feh WARNING: asdf.jxl - Does not look like an image (magic bytes missing)
```
To avoid this, set `FEH_SKIP_MAGIC=1` in feh's environment, and it will start working.

This workaround is no longer necessary since feh version 3.7.1, which recognises JXL's signature.


### Copying ###
All source code in this repository is available under the [BSD-3-Clause License](https://github.com/alistair7/imlib2-jxl/blob/main/LICENSE-BSD-ab), © Alistair Barrow
