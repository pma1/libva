/*
 * Copyright (c) 2007-2011 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL INTEL AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file va_vpp.h
 * \brief The video processing API
 *
 * This file contains the \ref api_vpp "Video processing API".
 */

#ifndef VA_VPP_H
#define VA_VPP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup api_vpp Video processing API
 *
 * @{
 *
 * The video processing API uses the same paradigm as for decoding:
 * - Query for supported capabilities;
 * - Set up a video processing pipeline;
 * - Send video processing parameters through VA buffers.
 *
 * \section api_vpp_caps Query for supported capabilities
 *
 * Checking whether video processing is supported can be performed
 * with vaQueryConfigEntrypoints() and the profile argument set to
 * #VAProfileNone.
 *
 * \code
 * VAEntrypoint *entrypoints;
 * int i, num_entrypoints, supportsVideoProcessing = 0;
 *
 * num_entrypoints = vaMaxNumEntrypoints();
 * entrypoints = malloc(num_entrypoints * sizeof(entrypoints[0]);
 * vaQueryConfigEntrypoints(va_dpy, VAProfileNone,
 *     entrypoints, &num_entrypoints);
 *
 * for (i = 0; !supportsVideoProcessing && i < num_entrypoints; i++) {
 *     if (entrypoints[i] == VAEntrypointVideoProc)
 *         supportsVideoProcessing = 1;
 * }
 * \endcode
 *
 * Then, video processing pipeline capabilities, i.e. which video
 * filters does the driver support, can be checked with the
 * vaQueryVideoProcPipelineCaps() function.
 *
 * \code
 * VAProcPipelineCap pipeline_caps[VAProcFilterCount];
 * unsigned int num_pipeline_caps = VAProcFilterCount;
 *
 * // num_pipeline_caps shall be initialized to the length of the array
 * vaQueryVideoProcPipelineCaps(va_dpy, vpp_ctx, &pipe_caps, &num_pipeline_caps);
 * \endcode
 *
 * Finally, individual filter capabilities can be checked with
 * vaQueryVideoProcFilterCaps().
 *
 * \code
 * VAProcFilterCap denoise_caps;
 * unsigned int num_denoise_caps = 1;
 * vaQueryVideoProcFilterCaps(va_dpy, vpp_ctx,
 *     VAProcFilterNoiseReduction,
 *     &denoise_caps, &num_denoise_caps
 * );
 *
 * VAProcFilterCapDeinterlacing deinterlacing_caps[VAProcDeinterlacingCount];
 * unsigned int num_deinterlacing_caps = VAProcDeinterlacingCount;
 * vaQueryVideoProcFilterCaps(va_dpy, vpp_ctx,
 *     VAProcFilterDeinterlacing,
 *     &deinterlacing_caps, &num_deinterlacing_caps
 * );
 * \endcode
 *
 * \section api_vpp_setup Set up a video processing pipeline
 *
 * A video processing pipeline buffer is created for each source
 * surface we want to process. However, buffers holding filter
 * parameters can be created once and for all. Rationale is to avoid
 * multiple creation/destruction chains of filter buffers and also
 * because filter parameters generally won't change frame after
 * frame. e.g. this makes it possible to implement a checkerboard of
 * videos where the same filters are applied to each video source.
 *
 * The general control flow is demonstrated by the following pseudo-code:
 * \code
 * // Create filters
 * VABufferID denoise_filter, deint_filter;
 * VABufferID filter_bufs[VAProcFilterCount];
 * unsigned int num_filter_bufs;
 *
 * for (i = 0; i < num_pipeline_caps; i++) {
 *     VAProcPipelineCap * const pipeline_cap = &pipeline_caps[i];
 *     switch (pipeline_cap->type) {
 *     case VAProcFilterNoiseReduction: {       // Noise reduction filter
 *         VAProcFilterParameterBuffer denoise;
 *         denoise.type  = VAProcFilterNoiseReduction;
 *         denoise.value = 0.5;
 *         vaCreateBuffer(va_dpy, vpp_ctx,
 *             VAProcFilterParameterBufferType, sizeof(denoise), 1,
 *             &denoise, &denoise_filter
 *         );
 *         filter_bufs[num_filter_bufs++] = denoise_filter;
 *         break;
 *     }
 *
 *     case VAProcFilterDeinterlacing:          // Motion-adaptive deinterlacing
 *         for (j = 0; j < num_deinterlacing_caps; j++) {
 *             VAProcFilterCapDeinterlacing * const cap = &deinterlacing_caps[j];
 *             if (cap->type != VAProcDeinterlacingMotionAdaptive)
 *                 continue;
 *
 *             VAProcFilterParameterBufferDeinterlacing deint;
 *             deint.type                   = VAProcFilterDeinterlacing;
 *             deint.algorithm              = VAProcDeinterlacingMotionAdaptive;
 *             deint.forward_references     =
 *                 malloc(cap->num_forward_references * sizeof(VASurfaceID));
 *             deint.num_forward_references = 0; // none for now
 *             deint.backward_references    =
 *                 malloc(cap->num_backward_references * sizeof(VASurfaceID));
 *             deint.num_backward_references = 0; // none for now
 *             vaCreateBuffer(va_dpy, vpp_ctx,
 *                 VAProcFilterParameterBufferType, sizeof(deint), 1,
 *                 &deint, &deint_filter
 *             );
 *             filter_bufs[num_filter_bufs++] = deint_filter;
 *         }
 *     }
 * }
 * \endcode
 *
 * \section api_vpp_submit Send video processing parameters through VA buffers
 *
 * Video processing pipeline parameters are submitted for each source
 * surface to process. Video filter parameters can also change, per-surface.
 * e.g. the list of reference frames used for deinterlacing.
 *
 * \code
 * foreach (iteration) {
 *     vaBeginPicture(va_dpy, vpp_ctx, vpp_surface);
 *     foreach (surface) {
 *         VARectangle output_region;
 *         VABufferID pipeline_buf;
 *         VAProcPipelineParameterBuffer *pipeline_param;
 *
 *         vaCreateBuffer(va_dpy, vpp_ctx,
 *             VAProcPipelineParameterBuffer, sizeof(*pipeline_param), 1,
 *             NULL, &pipeline_param
 *         );
 *
 *         // Setup output region for this surface
 *         // e.g. upper left corner for the first surface
 *         output_region.x     = BORDER;
 *         output_region.y     = BORDER;
 *         output_region.width =
 *             (vpp_surface_width - (Nx_surfaces + 1) * BORDER) / Nx_surfaces;
 *         output_region.height =
 *             (vpp_surface_height - (Ny_surfaces + 1) * BORDER) / Ny_surfaces;
 *
 *         vaMapBuffer(va_dpy, pipeline_buf, &pipeline_param);
 *         pipeline_param->surface              = surface;
 *         pipeline_param->surface_region       = NULL;
 *         pipeline_param->output_region        = &output_region;
 *         pipeline_param->output_background_color = 0;
 *         if (first surface to render)
 *             pipeline_param->output_background_color = 0xff000000; // black
 *         pipeline_param->flags                = VA_FILTER_SCALING_HQ;
 *         pipeline_param->filters              = filter_bufs;
 *         pipeline_param->num_filters          = num_filter_bufs;
 *         vaUnmapBuffer(va_dpy, pipeline_buf);
 *
 *         VAProcFilterParameterBufferDeinterlacing *deint_param;
 *         vaMapBuffer(va_dpy, deint_filter, &deint_param);
 *         // Update deinterlacing parameters, if necessary
 *         ...
 *         vaUnmapBuffer(va_dpy, deint_filter);
 *
 *         // Apply filters
 *         vaRenderPicture(va_dpy, vpp_ctx, &pipeline_buf, 1);
 *     }
 *     vaEndPicture(va_dpy, vpp_ctx);
 * }
 * \endcode
 */

#if 0
    /* Surfaces composition */
    VAProcPipelineParameterBuffer *pipeline_param;
    pipeline_param->surface                     = VA_INVALID_SURFACE;
    pipeline_param->surface_region              = surface_region;
    pipeline_param->output_region               = output_region;
    pipeline_param->output_background_color     = output_background_color;
    pipeline_param->flags                       = VA_FILTER_SCALING_HQ;
    pipeline_param->filters                     = filters;
    pipeline_param->num_filters                 = num_filters;

    VAProcPipelineID vpp_proc;
    VAProcPipelineParameterBuffer

    VAProcFilterParameterBuffer filter;

    vaBeginPicture(va_dpy, vpp_context, vpp_surface);
    {
        vaRenderPicture(va_dpy,
    }
    vaEndPicture(va_dpy, vpp_context);
#endif

/** \brief Video filter types. */
typedef enum _VAProcFilterType {
    VAProcFilterNone = 0,
    /** \brief Noise reduction filter. */
    VAProcFilterNoiseReduction,
    /** \brief Deinterlacing filter. */
    VAProcFilterDeinterlacing,
    /** \brief Sharpening filter. */
    VAProcFilterSharpening,
    /** \brief Color balance parameters. */
    VAProcFilterColorBalance,
    /** \brief Color standard conversion. */
    VAProcFilterColorStandard,
    /** \brief Max number of video filters. */
    VAProcFilterCount
} VAProcFilterType;

/** \brief Deinterlacing types. */
typedef enum _VAProcDeinterlacingType {
    VAProcDeinterlacingNone = 0,
    /** \brief Bob deinterlacing algorithm. */
    VAProcDeinterlacingBob,
    /** \brief Weave deinterlacing algorithm. */
    VAProcDeinterlacingWeave,
    /** \brief Motion adaptive deinterlacing algorithm. */
    VAProcDeinterlacingMotionAdaptive,
    /** \brief Motion compensated deinterlacing algorithm. */
    VAProcDeinterlacingMotionCompensated,
    /** \brief Max number of deinterlacing algorithms. */
    VAProcDeinterlacingCount
} VAProcDeinterlacingType;

/** \brief Color balance types. */
typedef enum _VAProcColorBalanceType {
    VAProcColorBalanceNone = 0,
    /** \brief Hue. */
    VAProcColorBalanceHue,
    /** \brief Saturation. */
    VAProcColorBalanceSaturation,
    /** \brief Brightness. */
    VAProcColorBalanceBrightness,
    /** \brief Contrast. */
    VAProcColorBalanceContrast,
    /** \brief Max number of color balance operations. */
    VAProcColorBalanceCount
} VAProcColorBalanceType;

/** \brief Color standard types. */
typedef enum _VAProcColorStandardType {
    VAProcColorStandardNone = 0,
    /** \brief ITU-R BT.601. */
    VAProcColorStandardBT601,
    /** \brief ITU-R BT.709. */
    VAProcColorStandardBT709,
    /** \brief ITU-R BT.470-2 System M. */
    VAProcColorStandardBT470M,
    /** \brief ITU-R BT.470-2 System B, G. */
    VAProcColorStandardBT470BG,
    /** \brief SMPTE-170M. */
    VAProcColorStandardSMPTE170M,
    /** \brief SMPTE-240M. */
    VAProcColorStandardSMPTE240M,
    /** \brief Generic film. */
    VAProcColorStandardGenericFilm,
} VAProcColorStandardType;

/** @name Video filter flags */
/**@{*/
/** \brief Specifies whether the filter shall be present in the pipeline. */
#define VA_PROC_FILTER_MANDATORY        0x00000001
/**@}*/

/** \brief Video processing pipeline capabilities. */
typedef struct _VAProcPipelineCap {
    /** \brief Video filter type. */
    VAProcFilterType    type;
    /** \brief Video filter flags. See video filter flags. */
    unsigned int        flags;
} VAProcPipelineCap;

/** \brief Specification of values supported by the filter. */
typedef struct _VAProcFilterValueRange {
    /** \brief Minimum value supported, inclusive. */
    float               min_value;
    /** \brief Maximum value supported, inclusive. */
    float               max_value;
    /** \brief Default value. */
    float               default_value;
    /** \brief Step value that alters the filter behaviour in a sensible way. */
    float               step;
} VAProcFilterValueRange;

/** \brief Video processing pipeline configuration. */
typedef struct _VAProcPipelineParameterBuffer {
    /** \brief Source surface ID. */
    VASurfaceID         surface;
    /**
     * \brief Region within the source surface to be processed.
     *
     * Pointer to a #VARectangle defining the region within the source
     * surface to be processed. If NULL, \c surface_region implies the
     * whole surface.
     */
    const VARectangle  *surface_region;
    /**
     * \brief Region within the output surface.
     *
     * Pointer to a #VARectangle defining the region within the output
     * surface that receives the processed pixels. If NULL, \c output_region
     * implies the whole surface. 
     *
     * Note that any pixels residing outside the specified region will
     * be filled in with the \ref output_background_color.
     */
    const VARectangle  *output_region;
    /**
     * \brief Background color.
     *
     * Background color used to fill in pixels that reside outside of the
     * specified \ref output_region. The color is specified in ARGB format:
     * [31:24] alpha, [23:16] red, [15:8] green, [7:0] blue.
     */
    unsigned int        output_background_color;
    /**
     * \brief Pipeline flags. See vaPutSurface() flags.
     *
     * Pipeline flags:
     * - Bob-deinterlacing: \c VA_FRAME_PICTURE, \c VA_TOP_FIELD,
     *   \c VA_BOTTOM_FIELD. Note that any deinterlacing filter
     *   (#VAProcFilterDeinterlacing) will override those flags.
     * - Color space conversion: \c VA_SRC_BT601, \c VA_SRC_BT709,
     *   \c VA_SRC_SMPTE_240. Note that any color standard filter
     *   (#VAProcFilterColorStandard) will override those flags.
     * - Scaling: \c VA_FILTER_SCALING_DEFAULT, \c VA_FILTER_SCALING_FAST,
     *   \c VA_FILTER_SCALING_HQ, \c VA_FILTER_SCALING_NL_ANAMORPHIC.
     */
    unsigned int        flags;
    /**
     * \brief Array of filters to apply to the surface.
     *
     * The list of filters shall be ordered in the same way the driver expects
     * them. i.e. as was returned from vaQueryVideoProcPipelineCaps().
     * Otherwise, a #VA_STATUS_ERROR_INVALID_FILTER_CHAIN is returned
     * from vaRenderPicture() with this buffer.
     *
     * #VA_STATUS_ERROR_UNSUPPORTED_FILTER is returned if the list
     * contains an unsupported filter.
     *
     * Note: no filter buffer is destroyed after a call to vaRenderPicture(),
     * only this pipeline buffer will be destroyed as per the core API
     * specification. This allows for flexibility in re-using the filter for
     * other surfaces to be processed.
     */
    VABufferID         *filters;
    /** \brief Actual number of filters. */
    unsigned int        num_filters;
} VAProcPipelineParameterBuffer;

/**
 * \brief Filter parameter buffer base.
 *
 * This is a helper structure used by driver implementations only.
 * Users are not supposed to allocate filter parameter buffers of this
 * type.
 */
typedef struct _VAProcFilterParameterBufferBase {
    /** \brief Filter type. */
    VAProcFilterType    type;
} VAProcFilterParameterBufferBase;

/**
 * \brief Default filter parametrization.
 *
 * Unless there is a filter-specific parameter buffer,
 * #VAProcFilterParameterBuffer is the default type to use.
 */
typedef struct _VAProcFilterParameterBuffer {
    /** \brief Filter type. */
    VAProcFilterType    type;
    /** \brief Value. */
    /* XXX: use VAGenericValue? */
    float               value;
} VAProcFilterParameterBuffer;

/** \brief Deinterlacing filter parametrization. */
typedef struct _VAProcFilterParameterBufferDeinterlacing {
    /** \brief Filter type. Shall be set to #VAProcFilterDeinterlacing. */
    VAProcFilterType            type;
    /** \brief Deinterlacing algorithm. */
    VAProcDeinterlacingType     algorithm;
    /** \brief Array of forward reference frames. */
    VASurfaceID                *forward_references;
    /** \brief Number of forward reference frames that were supplied. */
    unsigned int                num_forward_references;
    /** \brief Array of backward reference frames. */
    VASurfaceID                *backward_references;
    /** \brief Number of backward reference frames that were supplied. */
    unsigned int                num_backward_references;
} VAProcFilterParameterBufferDeinterlacing;

/**
 * \brief Color balance filter parametrization.
 *
 * This buffer defines color balance attributes. A VA buffer can hold
 * several color balance attributes by creating a VA buffer of desired
 * number of elements. This can be achieved by the following pseudo-code:
 *
 * \code
 * enum { kHue, kSaturation, kBrightness, kContrast };
 *
 * // Initial color balance parameters
 * static const VAProcFilterParameterBufferColorBalance colorBalanceParams[4] =
 * {
 *     [kHue] =
 *         { VAProcFilterColorBalance, VAProcColorBalanceHue, 0.5 },
 *     [kSaturation] =
 *         { VAProcFilterColorBalance, VAProcColorBalanceSaturation, 0.5 },
 *     [kBrightness] =
 *         { VAProcFilterColorBalance, VAProcColorBalanceBrightness, 0.5 },
 *     [kSaturation] =
 *         { VAProcFilterColorBalance, VAProcColorBalanceSaturation, 0.5 }
 * };
 *
 * // Create buffer
 * VABufferID colorBalanceBuffer;
 * vaCreateBuffer(va_dpy, vpp_ctx,
 *     VAProcFilterParameterBufferType, sizeof(*pColorBalanceParam), 4,
 *     colorBalanceParams,
 *     &colorBalanceBuffer
 * );
 *
 * VAProcFilterParameterBufferColorBalance *pColorBalanceParam;
 * vaMapBuffer(va_dpy, colorBalanceBuffer, &pColorBalanceParam);
 * {
 *     // Change brightness only
 *     pColorBalanceBuffer[kBrightness].value = 0.75;
 * }
 * vaUnmapBuffer(va_dpy, colorBalanceBuffer);
 * \endcode
 */
typedef struct _VAProcFilterParameterBufferColorBalance {
    /** \brief Filter type. Shall be set to #VAProcFilterColorBalance. */
    VAProcFilterType            type;
    /** \brief Color balance attribute. */
    VAProcColorBalanceType      attrib;
    /** \brief Color balance value. */
    float                       value;
} VAProcFilterParameterBufferColorBalance;

/** \brief Color standard filter parametrization. */
typedef struct _VAProcFilterParameterBufferColorStandard {
    /** \brief Filter type. Shall be set to #VAProcFilterColorStandard. */
    VAProcFilterType            type;
    /** \brief Color standard to use. */
    VAProcColorStandardType     standard;
} VAProcFilterParameterBufferColorStandard;

/**
 * \brief Default filter cap specification (single range value).
 *
 * Unless there is a filter-specific cap structure, #VAProcFilterCap is the
 * default type to use for output caps from vaQueryVideoProcFilterCaps().
 */
typedef struct _VAProcFilterCap {
    /** \brief Range of supported values for the filter. */
    VAProcFilterValueRange      range;
} VAProcFilterCap;

/** \brief Capabilities specification for the deinterlacing filter. */
typedef struct _VAProcFilterCapDeinterlacing {
    /** \brief Deinterlacing algorithm. */
    VAProcDeinterlacingType     type;
    /** \brief Number of forward references needed for deinterlacing. */
    unsigned int                num_forward_references;
    /** \brief Number of backward references needed for deinterlacing. */
    unsigned int                num_backward_references;
} VAProcFilterCapDeinterlacing;

/** \brief Capabilities specification for the color balance filter. */
typedef struct _VAProcFilterCapColorBalance {
    /** \brief Color balance operation. */
    VAProcColorBalanceType      type;
    /** \brief Range of supported values for the specified operation. */
    VAProcFilterValueRange      range;
} VAProcFilterCapColorBalance;

/** \brief Capabilities specification for the color standard filter. */
typedef struct _VAProcFilterCapColorStandard {
    /** \brief Color standard type. */
    VAProcColorStandardType     type;
} VAProcFilterCapColorStandard;

/**
 * \brief Queries video processing pipeline capabilities.
 *
 * This function returns the list of video processing filters supported
 * by the driver. The \c pipeline_caps array is allocated by the user and
 * \c num_pipeline_caps shall be initialized to the number of allocated
 * elements in that array. Upon successful return, the actual number
 * of filters will be overwritten into \c num_pipeline_caps. Otherwise,
 * \c VA_STATUS_ERROR_MAX_NUM_EXCEEDED is returned and \c num_pipeline_caps
 * is adjusted to the number of elements that would be returned if enough
 * space was available.
 *
 * The list of video processing filters supported by the driver shall
 * be ordered in the way they can be iteratively applied. This is needed
 * for both correctness, i.e. some filters would not mean anything if
 * applied at the beginning of the pipeline; but also for performance
 * since some filters can be applied in a single pass (e.g. noise
 * reduction + deinterlacing).
 *
 * @param[in] dpy               the VA display
 * @param[in] context           the video processing context
 * @param[out] pipeline_caps    the output array of #VAProcPipelineCap elements
 * @param[in,out] num_pipeline_caps the number of elements allocated on input,
 *      the number of elements actually filled in on output
 */
VAStatus
vaQueryVideoProcPipelineCaps(
    VADisplay           dpy,
    VAContextID         context,
    VAProcPipelineCap  *pipeline_caps,
    unsigned int       *num_pipeline_caps
);

/**
 * \brief Queries video filter capabilities.
 *
 * This function returns the list of capabilities supported by the driver
 * for a specific video filter. The \c filter_caps array is allocated by
 * the user and \c num_filter_caps shall be initialized to the number
 * of allocated elements in that array. Upon successful return, the
 * actual number of filters will be overwritten into \c num_filter_caps.
 * Otherwise, \c VA_STATUS_ERROR_MAX_NUM_EXCEEDED is returned and
 * \c num_filter_caps is adjusted to the number of elements that would be
 * returned if enough space was available.
 *
 * @param[in] dpy               the VA display
 * @param[in] context           the video processing context
 * @param[in] type              the video filter type
 * @param[out] filter_caps      the output array of #VAProcFilterCap elements
 * @param[in,out] num_filter_caps the number of elements allocated on input,
 *      the number of elements actually filled in output
 */
VAStatus
vaQueryVideoProcFilterCaps(
    VADisplay           dpy,
    VAContextID         context,
    VAProcFilterType    type,
    void               *filter_caps,
    unsigned int        num_filter_caps
);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* VA_VPP_H */