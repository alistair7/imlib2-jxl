/** @file imlib2-jxl.c
    @brief imlib2 loader module for JPEG XL

    @author Alistair Barrow
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>

#include "loader.h"

/**
 * Convenience macro that prints a message to stderr and jumps to the "ret" label.
 */
#define RETURN_ERR(...) \
do { \
    myprintf(stderr, __FILE__, __func__, __LINE__, __VA_ARGS__); \
    goto ret; \
}while(0)


/**
 * Macro to test current machine's endianness.
 * Whether this is a compile-time constant or generates executable code is
 * dependent on your compiler.
 */
#ifdef __GNUC__
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define IS_BIG_ENDIAN() false
  #else
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      #define IS_BIG_ENDIAN() true
    #else
      #error Unsupported endianness - sorry!
    #endif
  #endif
#else
  #define IS_BIG_ENDIAN() (!*(uint8_t*)&(uint16_t){1})
#endif


/* Debugging bumf */
#define WARN_PRINTF(...) myprintf(stderr, __FILE__, __func__, __LINE__, __VA_ARGS__)
#ifdef IMLIB2JXL_DEBUG
#define DEBUG_PRINTF(...) myprintf(stderr, __FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define DEBUG_PRINTF(...)
#endif



/**
 * Print a formatted message to a file.
 */
static void myprintf(FILE *to, const char *file, const char *func, unsigned line, const char *format, ...)
{
    fprintf(to, "%s: in function '%s':%u: ", file, func, line);
    va_list args;
    va_start(args, format);
    vfprintf(to, format, args);
    va_end(args);
    fputc('\n', to);
}

/**
 * @brief Function called by imlib2 to ask your loader what formats it supports.
 *
 * Set @c l->num_formats to the number of supported formats.
 *
 * Set @c l->formats to a heap-allocated array of <tt>char*</tt> with a number of
 * elements equal to @c l->num_formats.
 *
 * Each element of @c l->formats should point to a heap-allocated string defining
 * the format.
 */
void formats(ImlibLoader *l)
{
    DEBUG_PRINTF("");

    // imlib2 just defines DATA32 as an unsigned int, which isn't necessarily 32 bits.  So add a check just in case...
    // Note the optimizer removes this code entirely if the condition is false.
    if(sizeof(DATA32) != sizeof(uint32_t))
    {
        WARN_PRINTF("Loader relies on unsigned ints being 4 bytes long, but they're %zu B long.", sizeof(DATA32));
        l->num_formats = 0;
        l->formats = NULL;
    }

    static const char jxl[] = "jxl";
    l->num_formats = 1;
    l->formats = malloc(sizeof(char*));
    l->formats[0] = malloc(sizeof(jxl));
    memcpy(l->formats[0], jxl, sizeof(jxl));
}

/**
 * @brief Function called by imlib2 when it wants your loader to decode an image file.
 *
 * This function should read the input file (@c im->real_file), produce an array of raw pixel data,
 * and assign this to @c im->data.
 * The required format is described by this extract from imlib2's API documentation:<br/>
 *
 *     The image data is returned in the format of a DATA32 (32 bits) per pixel in a linear array ordered from the top
 *     left of the image to the bottom right going from left to right each line. Each pixel has the upper 8 bits as
 *     the alpha channel and the lower 8 bits are the blue channel - so a pixel's bits are ARGB (from most to least
 *     significant, 8 bits per channel.
 *
 * So on a little endian architecture, the required byte layout is B G R A.
 *
 * By default, imlib2 will pass @c im->data to <tt>free()</tt> when it has finished with the pixels.
 * You can prevent that by setting the @c F_DONT_FREE_DATA flag.
 *
 * @param[in] im->real_file The name of the file we should open and read.
 * @param[in] im->file ??? (use @c real_file instead.)
 * @param[out] im->data Make this point to the decoded pixel data, always 32-bit ARGB.
 * @param[out] im->format Make this point to a string describing the decoded file format,
 *             or leave as @c NULL to let imlib2 use the loader's default.
 * @param[out] im->w,im->h Image width and height, respectively, in pixels.
 * @param[out] im->flags A bitwise combination of @c ImlibImageFlags setting various properties
 *             of the image.  Important ones are @c F_HAS_ALPHA, which should be set if the
 *             decoded image isn't fully opaque; and @c F_DONT_FREE_DATA, which tells imlib2
 *             not to <tt>free(im->data)</tt>, in case we want to manage the memory ourselves.
 *
 * @param[in] progress Callback function which, if set, we're supposed to call periodically to
 *                     let the caller know much of the decoding is complete.
 * @param[in] progress_granularity ???
 * @param[in] immediate_load If non-zero, the caller wants us to decode the whole image and set
 *                           @c im->data to point to the allocated data.
 *                           If zero, we should not decode the whole image, and instead return
 *                           as soon as we've set the metadata (@c im->w, @c im->h, @c im->format).
 *
 * @return Non-zero on success
 * @return 0 on failure
 */
char load(ImlibImage *im, ImlibProgressFunction progress, char progress_granularity, char immediate_load)
{
#ifdef __GNUC__
    (void)progress_granularity;
#endif

    DEBUG_PRINTF("Read [%s] immediate_load[%d]", im->real_file, (int)immediate_load);

    char retval = 0;
    JxlDecoder *dec = NULL;
    void *runner = NULL;
    FILE *in = NULL;
    char *file_data = NULL;
    uint8_t *pixels = NULL;
    size_t pixels_size;

    // Initialize decoder
    if(im->data)
        RETURN_ERR("ImlibImage data field is not NULL");

    if(!(dec = JxlDecoderCreate(NULL)))
        RETURN_ERR("Failed in JxlDecoderCreate");

    if(!(runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads())))
        RETURN_ERR("Failed in JxlThreadParallelRunnerCreate");

    if(JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner) != JXL_DEC_SUCCESS)
        RETURN_ERR("Failed in JxlDecoderSetParallelRunner");

    if(JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
        RETURN_ERR("Failed in JxlDecoderSubscribeEvents");

    if(!(in = fopen(im->real_file, "rb")))
        RETURN_ERR("Failed to open %s for reading; %s", im->real_file, strerror(errno));

    // Buffer input file
    if(fseek(in, 0, SEEK_END) != 0)
        RETURN_ERR("Input file isn't seekable");
    size_t data_len = ftell(in);
    fseek(in, 0, SEEK_SET);
    if(!(file_data = malloc(data_len)))
        RETURN_ERR("Failed to allocate %zu B", data_len);
    if(fread(file_data, 1, data_len, in) != data_len)
        RETURN_ERR("Failed to read %zu B", data_len);
    fclose(in);
    in = NULL;

    if(JxlDecoderSetInput(dec, (const uint8_t*)file_data, data_len) != JXL_DEC_SUCCESS)
        RETURN_ERR("Failed in JxlDecoderSetInput");


    // Start decoding
    JxlDecoderStatus res;
    JxlBasicInfo basic_info;
    JxlPixelFormat pixel_format = {
                                    .num_channels = 4, // Data arrives as RGBA in that order, regardless of endianness
                                    .data_type = JXL_TYPE_UINT8,
                                    .endianness = JXL_NATIVE_ENDIAN,
                                    .align = 0
                                  };

    while((res = JxlDecoderProcessInput(dec)) != JXL_DEC_FULL_IMAGE)
    {

        switch(res)
        {
        case JXL_DEC_BASIC_INFO:

            if((res = JxlDecoderGetBasicInfo(dec, &basic_info)) != JXL_DEC_SUCCESS)
                RETURN_ERR("Failed in JxlDecoderGetBasicInfo");

            DEBUG_PRINTF("%ux%u RGB%s", basic_info.xsize, basic_info.ysize, basic_info.alpha_bits>0 ? "A" : "");

            if(!IMAGE_DIMENSIONS_OK(basic_info.xsize, basic_info.ysize))
                RETURN_ERR("Dimensions %ux%u are not supported by imlib2", basic_info.xsize, basic_info.ysize);

            im->w = basic_info.xsize;
            im->h = basic_info.ysize;

            if(basic_info.alpha_bits > 0)
                SET_FLAG(im->flags, F_HAS_ALPHA);

            // If imlib2 only wants the metadata, return now
            if (!immediate_load)
            {
                retval = 1;
                goto ret;
            }

            if(progress)
                progress(im, 1, 0, 0, basic_info.xsize, basic_info.ysize); // 1% done!
            break;

        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            // Time to allocate some space for the pixels
            if (JxlDecoderImageOutBufferSize(dec, &pixel_format, &pixels_size) != JXL_DEC_SUCCESS )
                RETURN_ERR("Failed in JxlDecoderImageOutBufferSize");

            // Sanity check
            if (pixels_size != (size_t)basic_info.xsize * basic_info.ysize * pixel_format.num_channels)
                RETURN_ERR("Pixel buffer size is %zu, but expected (%u * %u * %u) = %zu",
                           pixels_size, basic_info.xsize, basic_info.ysize, pixel_format.num_channels,
                           (size_t)basic_info.xsize * basic_info.ysize * pixel_format.num_channels);

            if(!(pixels = malloc(pixels_size)))
                RETURN_ERR("Failed to allocate %zu B", pixels_size);

            if (JxlDecoderSetImageOutBuffer(dec, &pixel_format, pixels, pixels_size) != JXL_DEC_SUCCESS)
                RETURN_ERR("Failed in JxlDecoderSetImageOutBuffer");

            break;

        case JXL_DEC_NEED_MORE_INPUT:
            RETURN_ERR("Input truncated");

        case JXL_DEC_ERROR:
        {
            JxlSignature sig = JxlSignatureCheck((uint8_t*)file_data, data_len);
            RETURN_ERR("Error while decoding: %s", (sig == JXL_SIG_CODESTREAM || sig == JXL_SIG_CONTAINER) ? "corrupted file?" : "not a JPEG XL file!");
        }

        default:
            RETURN_ERR("Unexpected result from JxlDecoderProcessInput");
      }

    }

    // Data from libjxl is byte-ordered RGBA, so now have to swap the channels around for imlib2.

    // Swap channels in place
    if(IS_BIG_ENDIAN())
    {
        // Convert RGBA to ARGB
        for(uint8_t *px = pixels; px < pixels+pixels_size; px += 4)
        {
            uint8_t tmp[4];
            memcpy(tmp, px, 4);
            px[0] = tmp[3];
            px[1] = tmp[0];
            px[2] = tmp[1];
            px[3] = tmp[2];
        }
    }
    else
    {
        // Convert RGBA to BGRA
        for(uint8_t *px = pixels; px < pixels+pixels_size; px += 4)
        {
            uint8_t tmp = px[0];
            px[0] = px[2];
            px[2] = tmp;
        }
    }

    im->data = (DATA32*)pixels;

    if(progress)
        progress(im, 100, 0, 0, basic_info.xsize, basic_info.ysize);

    retval = 1;

ret:
    if(retval == 0)
    {
       free(pixels);
       im->data = NULL;
    }
    free(file_data);
    if(in)
       fclose(in);
    if(dec)
        JxlDecoderDestroy(dec);
    if(runner)
        JxlThreadParallelRunnerDestroy(runner);

  return retval;
}



/**
 * @brief Function called by imlib2 when it wants your loader to write an image file.
 *
 * This function should feed the pixel data in @c im->data through the encoder, and
 * write the result to the file named @c im->real_file.
 *
 * See @ref load for the format of @c im->data.  The number of bytes in the data array
 * is equal to (4 * im->w * im->h).
 *
 * @param[in] im->real_file The name of the file we should open and write.
 * @param[in] im->file ??? (use @c real_file instead.)
 * @param[in] im->w,im->h Image width and height, respectively, in pixels.
 * @param[in] im->data Raw pixel data, always 32-bit ARGB.
 * @param[in] im->flags A bitwise combination of @c ImlibImageFlags setting various properties
 *             of the image.
 * @param[in] progress Callback function which, if set, we're supposed to call periodically to
 *                     let the caller know much of the encoding is complete.
 * @param[in] progress_granularity ???
 *
 * @return Non-zero on success
 * @return 0 on failure
 */
char save(ImlibImage *im, ImlibProgressFunction progress, char progress_granularity)
{
#ifdef __GNUC__
    (void)progress_granularity;
#endif

    char retval = 0;
    JxlEncoder *enc = NULL;
    void *runner = NULL;
    FILE *out = NULL;
    uint8_t *pixels = NULL;
    uint8_t *jxl_bytes = NULL;

    // Initialize encoder
    if(!(enc = JxlEncoderCreate(NULL)))
        RETURN_ERR("Failed in JxlEncoderCreate");

    if(!(runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads())))
        RETURN_ERR("Failed in JxlThreadParallelRunnerCreate");

    if(JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner) != JXL_ENC_SUCCESS)
        RETURN_ERR("Failed in JxlEncoderSetParallelRunner");

    JxlEncoderOptions *opts;
    if(!(opts = JxlEncoderOptionsCreate(enc, NULL)))
        RETURN_ERR("Failed in JxlEncoderOptionsCreate");

    JxlPixelFormat pixel_format = { .align = 0, .data_type = JXL_TYPE_UINT8, .num_channels = 4, .endianness = JXL_NATIVE_ENDIAN};

    JxlBasicInfo basic_info = {.alpha_bits = 8,
                               .bits_per_sample = 8,
                               .num_color_channels = 3,
                               .num_extra_channels = 1,
                               .orientation = JXL_ORIENT_IDENTITY,
                               .xsize = im->w,
                               .ysize = im->h,
                              };

    if(JxlEncoderSetBasicInfo(enc, &basic_info) != JXL_ENC_SUCCESS)
        RETURN_ERR("Failed to set encoder parameters with dimensions %d x %d", im->w, im->h);

    // Check for specific quality/compression parameters
    ImlibImageTag *tag;

    if((tag = __imlib_GetTag(im, "quality")))
    {
        // Other loaders seem to assume that quality is in the range [0-99] (?)
        const int max_quality = 99;

        int quality = tag->val;
        if(quality < 0)
            quality = 0;
        else if(quality > max_quality)
            quality = max_quality;

        // Transform quality 0-99 to distance 15-0
        if(JxlEncoderOptionsSetDistance(opts, 15 - (quality * 15/(float)max_quality)) != JXL_ENC_SUCCESS)
            RETURN_ERR("Failed in JxlEncoderOptionsSetDistance: %.1f", 15 - (quality * 15/(float)max_quality));

        // If quality is maxed out, explicity enable lossless mode
        if(quality == max_quality)
        {
            if(JxlEncoderOptionsSetLossless(opts, 1) != JXL_ENC_SUCCESS)
                RETURN_ERR("Failed in JxlEncoderOptionsSetLossless");
        }

    }

    if((tag = __imlib_GetTag(im, "compression")))
    {
        // Other loaders seem to assume that compression is in the range [0-9] (?)
        const int max_compression = 9;

        int compression = tag->val;
        if(compression < 0)
            compression = 0;
        else if(compression > max_compression)
            compression = max_compression;

        // Transform compression 0-9 to effort 3-9
        compression = 3 + (int)roundf(compression * 6/(float)max_compression);

        if(JxlEncoderOptionsSetEffort(opts, compression) != JXL_ENC_SUCCESS)
            RETURN_ERR("Failed in JxlEncoderOptionsSetEffort: %d", compression);
    }

    const size_t pixels_size = 4 * im->w * im->h;

    // Create a copy of the pixel data with the channels in the correct order

    if(!(pixels = malloc(pixels_size)))
        RETURN_ERR("Failed to allocate 4 * %d * %d = %zu B", im->w, im->h, pixels_size);

    // Data from imlib2 is 32-bit ARGB, so now have to swap the channels around for libjxl.

    uint8_t *im2_bytes = (uint8_t*)im->data;
    const bool have_alpha = (im->flags & F_HAS_ALPHA);

    if(IS_BIG_ENDIAN())
    {
        // Convert ARGB to RGBA
        for(size_t i=0; i<pixels_size; i+=4)
        {
            pixels[i+0] = im2_bytes[i+1];
            pixels[i+1] = im2_bytes[i+2];
            pixels[i+2] = im2_bytes[i+3];
            pixels[i+3] = (have_alpha ? im2_bytes[i+0] : 0xFF);
        }
    }
    else
    {
        // Convert BGRA to RGBA
        for(size_t i=0; i<pixels_size; i+=4)
        {
            pixels[i+0] = im2_bytes[i+2];
            pixels[i+1] = im2_bytes[i+1];
            pixels[i+2] = im2_bytes[i+0];
            pixels[i+3] = (have_alpha ? im2_bytes[i+3] : 0xFF);
        }
    }

    if(progress)
        progress(im, 1, 0, 0, im->w, im->h);

    // Tell encoder to use these pixels
    if(JxlEncoderAddImageFrame(opts, &pixel_format, pixels, pixels_size) != JXL_ENC_SUCCESS)
        RETURN_ERR("Failed in JxlEncoderAddImageFrame");

    // Tell encoder there are no more frames after this one
    JxlEncoderCloseInput(enc);

    // Open output file
    if(!(out = fopen(im->real_file, "wb")))
        RETURN_ERR("Failed to open %s for writing; %s", im->real_file, strerror(errno));

    // Create buffer for encoded bytes - it doesn't matter if it's too small (within reason)
    size_t jxl_bytes_size = pixels_size / 16;
    if(jxl_bytes_size < 8*1024)
        jxl_bytes_size = 8*1024;

    if(!(jxl_bytes = malloc(jxl_bytes_size)))
        RETURN_ERR("Failed to allocate %zu B", jxl_bytes_size);

    JxlEncoderStatus res;
    uint8_t *next_out = jxl_bytes;
    size_t avail_out = jxl_bytes_size;

    while((res = JxlEncoderProcessOutput(enc, &next_out, &avail_out)) != JXL_ENC_SUCCESS)
    {
       if(res == JXL_ENC_NEED_MORE_OUTPUT)
       {
           if(next_out == jxl_bytes)
               RETURN_ERR("Encoding stalled");

           // Flush what we've got to clear the output buffer and continue

           if(fwrite(jxl_bytes, 1, jxl_bytes_size - avail_out, out) != jxl_bytes_size-avail_out)
               RETURN_ERR("Failed to write %zu B", jxl_bytes_size - avail_out);

           next_out = jxl_bytes;
           avail_out = jxl_bytes_size;
       }
       else
       {
           RETURN_ERR("Error during encoding");
       }
    }

    if(fwrite(jxl_bytes, 1, jxl_bytes_size - avail_out, out) != jxl_bytes_size-avail_out)
        RETURN_ERR("Failed to write %zu B", jxl_bytes_size - avail_out);

    if(progress)
        progress(im, 100, 0, 0, im->w, im->h);

    retval = 1;

ret:
    free(pixels);
    free(jxl_bytes);
    if(out)
        fclose(out);
    if(enc)
        JxlEncoderDestroy(enc);
    if(runner)
        JxlThreadParallelRunnerDestroy(runner);
    return retval;
}

