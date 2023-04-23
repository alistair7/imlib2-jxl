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
#include <inttypes.h>

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>

#ifdef IMLIB2JXL_USE_LCMS
#include <lcms2.h>
#endif

// If your distribution doesn't provide this header with its imlib2 package,
// it's available at https://git.enlightenment.org/old/legacy-imlib2/src/branch/master/src/lib/Imlib2_Loader.h
#include "Imlib2_Loader.h"

/**
 * Convenience macro that prints a message to stderr and jumps to the "ret" label.
 */
#define RETURN_ERR(rv, ...) \
do { \
    myprintf(stderr, __FILE__, __func__, __LINE__, __VA_ARGS__); \
    retval = (rv); \
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

#ifdef __GNUC__
static void myprintf(FILE *to, const char *file, const char *func, unsigned line, const char *format, ...)
__attribute__(( format(printf,5,6) ));
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


static const char* const formats[] = { "jxl" };




#ifdef IMLIB2JXL_USE_LCMS


#ifdef IMLIB2JXL_DEBUG
/**
 * @brief Get readable description of ICC profile.
 *
 * The caller is responsible for freeing the returned pointer.
 *
 * @return Pointer to allocated string, or @c NULL on failure.
 */
static char *get_icc_description(cmsHPROFILE icc)
{
    if(LCMS_VERSION != cmsGetEncodedCMMversion())
        WARN_PRINTF("Warning: jxl loader was compiled against a different version of liblcms!");

    char *retval = NULL;
    const char *lang = "en";
    const char *country = "US";

    {
        char *lang_test = getenv("LANG");
        if(lang_test)
        {
            char lang_env[20];
            snprintf(lang_env, sizeof(lang_env), "%s", lang_test);
            char *lang_end = strchr(lang_env, '_');
            if(lang_end)
            {
                *lang_end = '\0';
                char *country_end = strchr(lang_end+1, '.');
                if(country_end)
                {
                    *country_end = '\0';
                    lang = lang_env;
                    country = lang_end + 1;
                    DEBUG_PRINTF("Got lang \"%s\", country \"%s\" from environment", lang, country);
                }
            }
        }
    }

    const cmsInfoType infoType = cmsInfoDescription;

    cmsUInt32Number required = cmsGetProfileInfoASCII(icc, infoType, lang, country, NULL, 0);
    if(!(retval = malloc(required)))
        RETURN_ERR(NULL, "Failed to allocate %u B for ICC description", (unsigned)required);

    cmsGetProfileInfoASCII(icc, infoType, lang, country, retval, required);

ret:
    return retval;
}
#endif // IMLIB2JXL_DEBUG


/**
 * @brief Convert pixels to sRGB from whatever profile they're currently using.
 *
 * The input is always 8-bit interleaved channels in a fixed order. The format is indicated by @p num_channels:
 * - num_channels == 1 : Gray
 * - num_channels == 2 : Gray + Alpha
 * - num_channels == 3 : RGB
 * - num_channels == 4 : RGB + Alpha
 * 
 * The output is always word-ordered ARGB, 4 bytes per pixel, as expected by imlib2.
 * 
 * TODO: Transforming integer pixels will incur rounding errors, but would using a float buffer be worth the overhead?
 * 
 * @param[in] input_icc_blob Pointer to ICC profile blob representing the current profile.
 * @param[in] icc_blob_size Number of bytes in the profile referenced by @p input_icc_blob.
 * @param[in] px_in Pointer to current pixel data.
 * @param[out] px_out Pointer to a buffer where the transformed pixel data will be written.
 * @param[in] num_pixels Number of pixels to transform. @p px_out should be at least `4 * num_pixels` bytes long.
 *
 * @return 0 on success.
 */
static int convert_to_srgb(uint8_t *input_icc_blob, size_t icc_blob_size, const void *px_in, void *px_out, size_t num_pixels, int num_channels)
{
    int retval = -1;
    cmsHPROFILE source_icc = NULL;
    cmsHPROFILE srgb_icc = NULL;
    cmsHTRANSFORM trans = NULL;
    cmsContext ctx = NULL;
    //cmsToneCurve* srgb_tonecurve = NULL;
#ifdef IMLIB2JXL_DEBUG
    char *src_icc_name = NULL;
    char *dst_icc_name = NULL;
#endif
    
    if(!(ctx = cmsCreateContext(NULL, NULL)))
        RETURN_ERR(-1, "Failed to create lcms context");

    if(!(source_icc = cmsOpenProfileFromMemTHR(ctx, input_icc_blob, icc_blob_size)))
        RETURN_ERR(-1, "Failed to create color profile from %zu B ICC data", icc_blob_size);

    if(!(srgb_icc = cmsCreate_sRGBProfileTHR(ctx)))
        RETURN_ERR(-1, "Failed to create sRGB color profile");
    
    cmsUInt32Number input_format;
    switch(num_channels)
    {
        case 3: input_format = TYPE_RGB_8; break;
        case 4: input_format = TYPE_RGBA_8; break;
        case 1: input_format = TYPE_GRAY_8; break;
        case 2: input_format = TYPE_GRAYA_8; break;
        default:
            RETURN_ERR(-1, "Unsupported number of channels (%d)", num_channels);
    }
    
    //if(is_gray)
    //{
    //    // No convenient function for creating a gray sRGB profile, so have to build it.
    //    static const cmsCIExyY d65 = {0.3127, 0.3291, 1.0};
    //    static const cmsFloat64Number srgbCurveParams[] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
    //    if(!(srgb_tonecurve =  cmsBuildParametricToneCurve(ctx, 4/*sRGB*/, srgbCurveParams)))
    //        RETURN_ERR(-1, "Failed to create sRGB tone curve");
    //    if(!(srgb_icc = cmsCreateGrayProfileTHR(ctx, &d65, srgb_tonecurve)))
    //        RETURN_ERR(-1, "Failed to create sRGB color profile");
    //    input_format = TYPE_GRAYA_8;
    //}

#ifdef IMLIB2JXL_DEBUG
    if((src_icc_name = get_icc_description(source_icc)) &&
       (dst_icc_name = get_icc_description(srgb_icc)))
    {
        DEBUG_PRINTF("Converting color space [%s] -> [%s]; num_pixels=%zu num_channels=%d",
                     src_icc_name, dst_icc_name, num_pixels, num_channels);
    }
#endif

    // To avoid shuffling the channels again later, set the output format to the required TYPE_ARGB_8

    if(!(trans = cmsCreateTransformTHR(ctx, source_icc, input_format, srgb_icc,
                                       IS_BIG_ENDIAN() ? TYPE_ARGB_8 : TYPE_BGRA_8,
                                       cmsGetHeaderRenderingIntent(source_icc), cmsFLAGS_COPY_ALPHA)))
    {
#ifdef IMLIB2JXL_DEBUG
        char *from = get_icc_description(source_icc);
        char *to = get_icc_description(srgb_icc);
        DEBUG_PRINTF("Failed to create color transformation [%s] -> [%s]", from, to);
        free(from);
        free(to);
#endif
        goto ret;
    }

    cmsDoTransform(trans, px_in, px_out, num_pixels);
    retval = 0;

ret:
    if(trans)
        cmsDeleteTransform(trans);
    if(srgb_icc)
        cmsCloseProfile(srgb_icc);
    if(source_icc)
        cmsCloseProfile(source_icc);
    //if(srgb_tonecurve)
    //    cmsFreeToneCurve(srgb_tonecurve);
    if(ctx)
        cmsDeleteContext(ctx);
#ifdef IMLIB2JXL_DEBUG
    free(src_icc_name);
    free(dst_icc_name);
#endif
    return retval;
}


/**
 * Return true if vectors are "roughly" equal.
 * i.e. no component differs by >= 2e-5.
 * This threshold is completely arbitrary.
 *
 * @param[in] length Dimension of both vectors.
 * @param[in] v1,v2 Arrays of length @p length to compare.
 */
static bool near_equal(unsigned length, const double *v1, const double *v2)
{
  for(unsigned i=0; i<length; ++i)
  {
      if(fabs(v1[i]-v2[i]) >= .00002)
          return false;
  }
  return true;
}

#endif // IMLIB2JXL_USE_LCMS


static int load(ImlibImage* im, int load_data)
{
    DEBUG_PRINTF("Load [%s][%zu]", im->fi->name, (size_t)im->fi->fsize);

    int retval = LOAD_FAIL;
    JxlDecoder *dec = NULL;
    void *runner = NULL;
    uint8_t *target = NULL;

#ifdef IMLIB2JXL_USE_LCMS
    uint8_t *icc_blob = NULL;
    size_t icc_size = 0;
    const int events = JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE | JXL_DEC_COLOR_ENCODING;
#else
    const int events = JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE;
#endif

    // Initialize decoder
    if(!(dec = JxlDecoderCreate(NULL)))
        RETURN_ERR(LOAD_FAIL, "Failed in JxlDecoderCreate");

    if(!(runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads())))
        RETURN_ERR(LOAD_FAIL, "Failed in JxlThreadParallelRunnerCreate");

    if(JxlDecoderSetParallelRunner(dec, JxlThreadParallelRunner, runner) != JXL_DEC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed in JxlDecoderSetParallelRunner");

    if(JxlDecoderSubscribeEvents(dec, events) != JXL_DEC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed in JxlDecoderSubscribeEvents");

    if(JxlDecoderSetInput(dec, (const uint8_t*)im->fi->fdata, im->fi->fsize) != JXL_DEC_SUCCESS)
        RETURN_ERR(LOAD_BADIMAGE, "Failed in JxlDecoderSetInput");

    
    // Start decoding
    size_t num_pixels = 0;
    size_t pixels_size = 0; // Total size of raw pixels in bytes
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
                RETURN_ERR(LOAD_BADIMAGE, "Failed in JxlDecoderGetBasicInfo");

            DEBUG_PRINTF("%ux%u RGB%s", basic_info.xsize, basic_info.ysize, basic_info.alpha_bits>0 ? "A" : "");

            if(!IMAGE_DIMENSIONS_OK(basic_info.xsize, basic_info.ysize))
                RETURN_ERR(LOAD_BADIMAGE, "Dimensions %ux%u are not supported by imlib2", basic_info.xsize, basic_info.ysize);

            im->w = basic_info.xsize;
            im->h = basic_info.ysize;
            num_pixels = basic_info.xsize * basic_info.ysize;
            im->has_alpha = basic_info.alpha_bits > 0;
            pixel_format.num_channels = ((basic_info.num_color_channels >= 3) ? 3 : 1) + (basic_info.alpha_bits > 0);
            
            // If imlib2 only wants the metadata, return now
            if (!load_data)
            {
                retval = LOAD_SUCCESS;
                goto ret;
            }

            break;

#ifdef IMLIB2JXL_USE_LCMS
        case JXL_DEC_COLOR_ENCODING:
        {
            //if(basic_info.num_color_channels < 3)
            //{
            //    /* Converting color profiles for grayscale input is currently broken, so skip for now. */
            //    DEBUG_PRINTF("Ignored color encoding for grayscale image");
            //    break;
            //}

            // If the decoder can produce srgb, it should.
            //JxlDecoderSetCms(); // not implemented in libjxl yet
            JxlColorEncoding srgb;
            JxlColorEncodingSetToSRGB(&srgb, /*is_gray=*/basic_info.num_color_channels == 1);
            if(JxlDecoderSetPreferredColorProfile(dec, &srgb) != JXL_DEC_SUCCESS)
                WARN_PRINTF("Cannot set preferred output color profile");

            /* If libjxl claims the decoded pixels will be RGB/sRGB, don't bother converting anything.
             * If there's no JPEG-XL-encoded profile, or it's something other than sRGB, try to
             * extract an ICC profile and save it for later. */

            JxlColorEncoding color_enc;
            if(JxlDecoderGetColorAsEncodedProfile(dec, &pixel_format, JXL_COLOR_PROFILE_TARGET_DATA, &color_enc) == JXL_DEC_SUCCESS)
            {

                if((color_enc.color_space == JXL_COLOR_SPACE_RGB || color_enc.color_space == JXL_COLOR_SPACE_GRAY)
                   &&
                   /* Transfer function is IEC sRGB */
                   color_enc.transfer_function == JXL_TRANSFER_FUNCTION_SRGB
                   &&
                   (color_enc.color_space == JXL_COLOR_SPACE_GRAY ||
                     (
                       /* Primaries are CIE sRGB or close enough */
                       color_enc.primaries == JXL_PRIMARIES_SRGB ||
                       (color_enc.primaries == JXL_PRIMARIES_CUSTOM &&
                         near_equal(2, color_enc.primaries_red_xy, srgb.primaries_red_xy) &&
                         near_equal(2, color_enc.primaries_green_xy, srgb.primaries_green_xy) &&
                         near_equal(2, color_enc.primaries_blue_xy, srgb.primaries_blue_xy)
                       )
                     )
                   )
                   &&
                   (
                    /* White point is, or could pass for, D65 */
                     color_enc.white_point == JXL_WHITE_POINT_D65 ||
                    (color_enc.white_point == JXL_WHITE_POINT_CUSTOM && near_equal(2, color_enc.white_point_xy, srgb.white_point_xy))
                   )
                  )
                {
                    DEBUG_PRINTF("Encoded color profile is %s %ssRGB/D65",
                                 color_enc.transfer_function == JXL_TRANSFER_FUNCTION_SRGB &&
                                 color_enc.white_point == JXL_WHITE_POINT_D65
                                 ? "exactly" : "nearly",
                                 color_enc.color_space == JXL_COLOR_SPACE_GRAY ? "(gray) " : "");
                    break;
                }
            }

            if(JxlDecoderGetICCProfileSize(dec, &pixel_format, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size) != JXL_DEC_SUCCESS)
            {
                icc_size = 0;
                break;
            }

            if(!(icc_blob = malloc(icc_size)))
                RETURN_ERR(LOAD_OOM, "Failed to allocate %zu B for ICC profile", icc_size);

            if(JxlDecoderGetColorAsICCProfile(dec, &pixel_format, JXL_COLOR_PROFILE_TARGET_DATA, icc_blob, icc_size) != JXL_DEC_SUCCESS)
            {
                WARN_PRINTF("Failed to read ICC profile");
                free(icc_blob);
                icc_blob = NULL;
                icc_size = 0;
                break;
            }

            DEBUG_PRINTF("Got ICC color profile");
            break;
        }
#endif // IMLIB2JXL_USE_LCMS

        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            // Time to allocate some space for the pixels
            if (JxlDecoderImageOutBufferSize(dec, &pixel_format, &pixels_size) != JXL_DEC_SUCCESS )
                RETURN_ERR(LOAD_FAIL, "Failed in JxlDecoderImageOutBufferSize");

            // Sanity check
            if (pixels_size != (size_t)basic_info.xsize * basic_info.ysize * pixel_format.num_channels)
                RETURN_ERR(LOAD_FAIL, "Pixel buffer size is %zu, but expected (%u * %u * %u) = %zu",
                           pixels_size, basic_info.xsize, basic_info.ysize, pixel_format.num_channels,
                           (size_t)basic_info.xsize * basic_info.ysize * pixel_format.num_channels);

            if (!(target = malloc(pixels_size * sizeof(uint8_t))))
                RETURN_ERR(LOAD_OOM, "Failed to allocate %zu B for pixels", pixels_size);

            if (JxlDecoderSetImageOutBuffer(dec, &pixel_format, target, pixels_size) != JXL_DEC_SUCCESS)
                RETURN_ERR(LOAD_FAIL, "Failed in JxlDecoderSetImageOutBuffer");

            break;

        case JXL_DEC_NEED_MORE_INPUT:
            RETURN_ERR(LOAD_BADIMAGE, "Input truncated");

        case JXL_DEC_ERROR:
        {
            JxlSignature sig = JxlSignatureCheck((uint8_t*)im->fi->fdata, im->fi->fsize);
            RETURN_ERR(LOAD_BADIMAGE, "Error while decoding: %s", (sig == JXL_SIG_CODESTREAM || sig == JXL_SIG_CONTAINER) ? "corrupted file?" : "not a JPEG XL file!");
        }

        default:
            RETURN_ERR(LOAD_FAIL, "Unexpected result from JxlDecoderProcessInput");
      }

    }

    // Allocate buffer for im->data
    if(!__imlib_AllocateData(im))
        RETURN_ERR(LOAD_OOM, "Failed in __imlib_AllocateData");

    // Data from libjxl is byte-ordered RGBA, so now have to swap the channels around for imlib2
    // ...but if we're doing a color space transformation, we can swap channels at the same time, so
    // there's no need to do two passes.

#ifdef IMLIB2JXL_USE_LCMS
    bool color_converted = false;

    if(icc_size > 0)
    {
        // Reinterpret im->data as a uint8_t*, which is unportable,
        // but awfully convenient when uint8_t == unsigned char.
        if(convert_to_srgb(icc_blob, icc_size, target, (uint8_t*)(im->data),
                           num_pixels, pixel_format.num_channels))
        {
            WARN_PRINTF("Color space transformation failed, but continuing anyway");
        }
        else
        {
            color_converted = true;
        }
    }

    if(!color_converted)
    {
#endif
        // Convert byte-ordered data in target to word-ordered ARGB
        if(pixel_format.num_channels == 4)
        {   // RGBA
            for (size_t i=0; i<num_pixels; ++i)
                im->data[i] = PIXEL_ARGB(target[4*i+3], target[4*i+0], target[4*i+1], target[4*i+2]);
        }
        else if(pixel_format.num_channels == 3)
        {   // RGB
            for (size_t i=0; i<num_pixels; ++i)
                im->data[i] = PIXEL_ARGB(255u, target[3*i+0], target[3*i+1], target[3*i+2]);
        }
        else if(pixel_format.num_channels == 2)
        {   // GrayA
            for (size_t i=0; i<num_pixels; ++i)
                im->data[i] = PIXEL_ARGB(target[2*i+1], target[2*i], target[2*i], target[2*i]);
        }
        else
        {   // Gray
            for (size_t i=0; i<num_pixels; ++i)
                im->data[i] = PIXEL_ARGB(255u, target[i], target[i], target[i]);
        }

#ifdef IMLIB2JXL_USE_LCMS
    }
#endif

    retval = LOAD_SUCCESS;

ret:
#ifdef IMLIB2JXL_USE_LCMS
    free(icc_blob);
#endif
    free(target);
    if(dec)
        JxlDecoderDestroy(dec);
    if(runner)
        JxlThreadParallelRunnerDestroy(runner);

  return retval;
}


static int save(ImlibImage* im)
{
    int retval = LOAD_FAIL;
    JxlEncoder *enc = NULL;
    void *runner = NULL;
    uint8_t *pixels = NULL;
    uint8_t *jxl_bytes = NULL;

    // Initialize encoder
    if(!(enc = JxlEncoderCreate(NULL)))
        RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderCreate");

    if(!(runner = JxlThreadParallelRunnerCreate(NULL, JxlThreadParallelRunnerDefaultNumWorkerThreads())))
        RETURN_ERR(LOAD_FAIL, "Failed in JxlThreadParallelRunnerCreate");

    if(JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner) != JXL_ENC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderSetParallelRunner");

    JxlEncoderFrameSettings *opts;
    if(!(opts = JxlEncoderFrameSettingsCreate(enc, NULL)))
        RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderFrameSettingsCreate");

    JxlPixelFormat pixel_format = {
                                      .align = 0,
                                      .data_type = JXL_TYPE_UINT8,
                                      .num_channels = 3,
                                      .endianness = JXL_NATIVE_ENDIAN
                                  };

    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = im->w;
    basic_info.ysize = im->h;
    basic_info.uses_original_profile = JXL_FALSE;
    if(im->has_alpha)
    {
        basic_info.alpha_bits = 8;
        basic_info.num_extra_channels = 1;
        pixel_format.num_channels = 4;
    }
    else
    {
        basic_info.alpha_bits = 0;
        basic_info.num_extra_channels = 0;
    }
    size_t num_pixels = basic_info.xsize * basic_info.ysize;
    
    // Check for specific quality/compression parameters
    ImlibImageTag *tag;

    if((tag = __imlib_GetTag(im, "quality")))
    {
        // Other loaders seem to assume that quality is in the range [0-99] (?)
        const int max_quality = 99;

        int quality = (tag->val < 0) ? 0 :
                      (tag->val > max_quality) ? max_quality :
                       tag->val;

        // If quality is maxed out, explicity enable lossless mode
        if(quality == max_quality)
        {
            basic_info.uses_original_profile = JXL_TRUE;
            if(JxlEncoderSetFrameLossless(opts, JXL_TRUE) != JXL_ENC_SUCCESS)
                RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderSetFrameLossless");
            DEBUG_PRINTF("Lossless encoding");
        }
        else
        {
            // Transform quality 0-99 to distance 15-0
            float distance = 15 - (quality * 15/(float)max_quality);
            if(JxlEncoderSetFrameDistance(opts, distance) != JXL_ENC_SUCCESS)
                RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderSetFrameDistance: %.1f", distance);
            DEBUG_PRINTF("Butteraugli distance = %.1f", distance);
        }
    }

    if((tag = __imlib_GetTag(im, "compression")))
    {
        // Other loaders seem to assume that compression is in the range [0-9] (?)
        // libjxl works with [1-9]

        int compression = (tag->val < 1) ? 1 :
                          (tag->val > 9) ? 9 :
                           tag->val;

        if(JxlEncoderFrameSettingsSetOption(opts, JXL_ENC_FRAME_SETTING_EFFORT, compression) != JXL_ENC_SUCCESS)
            RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderFrameSettingsSetOption(JXL_ENC_FRAME_SETTING_EFFORT, %d)", compression);

        DEBUG_PRINTF("Effort = %d", compression);
    }

    if(JxlEncoderSetBasicInfo(enc, &basic_info) != JXL_ENC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed to set encoder parameters with dimensions %d x %d", im->w, im->h);

    /* Switch to codestream level 10 if required */
    int level;
    if((level = JxlEncoderGetRequiredCodestreamLevel(enc)) == 10)
    {
        if(JxlEncoderSetCodestreamLevel(enc, level) != JXL_ENC_SUCCESS)
            RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderSetCodestreamLevel(%d)", level);
    }

    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    if(JxlEncoderSetColorEncoding(enc, &color) != JXL_ENC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed in JXLEncoderSetColorEncoding");

    const size_t pixels_size = pixel_format.num_channels * im->w * im->h;

    // Create a copy of the pixel data with the channels in the correct order

    if(!(pixels = malloc(pixels_size)))
        RETURN_ERR(LOAD_OOM, "Failed to allocate %" PRIu32 " * %" PRIu32 " * %" PRIu32 " = %zu B", pixel_format.num_channels, im->w, im->h, pixels_size);

    // Data from imlib2 is 32-bit ARGB, so now have to swap the channels around for libjxl.
    const uint32_t* impixel = im->data;
    if(pixel_format.num_channels == 3)
    {
        for(size_t i=0; i<num_pixels; ++i)
        {
            const uint32_t pixel = *(impixel++);
            pixels[i*3+0] = PIXEL_R(pixel);
            pixels[i*3+1] = PIXEL_G(pixel);
            pixels[i*3+2] = PIXEL_B(pixel);
        }
    }
    else
    {
        for(size_t i=0; i<num_pixels; ++i)
        {
            const uint32_t pixel = *(impixel++);
            pixels[i*4+0] = PIXEL_R(pixel);
            pixels[i*4+1] = PIXEL_G(pixel);
            pixels[i*4+2] = PIXEL_B(pixel);
            pixels[i*4+3] = PIXEL_A(pixel);
        }
    }

    // Tell encoder to use these pixels
    if(JxlEncoderAddImageFrame(opts, &pixel_format, pixels, pixels_size) != JXL_ENC_SUCCESS)
        RETURN_ERR(LOAD_FAIL, "Failed in JxlEncoderAddImageFrame");

    // Tell encoder there are no more frames after this one
    JxlEncoderCloseInput(enc);

    FILE* const out = im->fi->fp;

    // Create buffer for encoded bytes - it doesn't matter if it's too small (within reason)
    size_t jxl_bytes_size = pixels_size / 16;
    if(jxl_bytes_size < 8*1024)
        jxl_bytes_size = 8*1024;

    if(!(jxl_bytes = malloc(jxl_bytes_size)))
        RETURN_ERR(LOAD_OOM, "Failed to allocate %zu B", jxl_bytes_size);

    JxlEncoderStatus res;
    uint8_t *next_out = jxl_bytes;
    size_t avail_out = jxl_bytes_size;

    while((res = JxlEncoderProcessOutput(enc, &next_out, &avail_out)) != JXL_ENC_SUCCESS)
    {
        if(res == JXL_ENC_NEED_MORE_OUTPUT)
        {
            if(next_out == jxl_bytes)
                RETURN_ERR(LOAD_FAIL, "Encoding stalled");

            // Flush what we've got to clear the output buffer and continue

            if(fwrite(jxl_bytes, 1, jxl_bytes_size - avail_out, out) != jxl_bytes_size-avail_out)
                RETURN_ERR(LOAD_FAIL, "Failed to write %zu B", jxl_bytes_size - avail_out);

            next_out = jxl_bytes;
            avail_out = jxl_bytes_size;
        }
        else
        {
            RETURN_ERR(LOAD_FAIL, "Error during encoding");
        }
    }

    if(fwrite(jxl_bytes, 1, jxl_bytes_size - avail_out, out) != jxl_bytes_size-avail_out)
        RETURN_ERR(LOAD_FAIL, "Failed to write %zu B", jxl_bytes_size - avail_out);
    

    retval = LOAD_SUCCESS;

ret:
    free(pixels);
    free(jxl_bytes);
    if(enc)
        JxlEncoderDestroy(enc);
    if(runner)
        JxlThreadParallelRunnerDestroy(runner);
    return retval;
}


IMLIB_LOADER(formats, load, save);
