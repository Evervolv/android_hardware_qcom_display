/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Portions modified by Andrew Sutherland <dr3wsuth3rland@gmail.com> for
 * the Evervolv Project's qsd8k lineup
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ui/android_native_buffer.h>
#include <gralloc_priv.h>
#include <genlock.h>
#include <qcom_ui.h>
#include <copybit.h>

#define HWC_DEBUG 0
// Warning: below defines produce massive logcat output
#define HWC_DBG_DUMP_LAYER  0
#define HWC_DEBUG_COPYBIT 0
/*****************************************************************************/
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
};

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

struct private_hwc_module_t {
    hwc_module_t base;
    copybit_device_t *copybitEngine;
    framebuffer_device_t *fbDevice;
    int compositionType;
};

struct private_hwc_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: HWC_HARDWARE_MODULE_ID,
            name: "Hardware Composer Module",
            author: "The Android Open Source Project",
            methods: &hwc_module_methods,
        }
   },
   copybitEngine: NULL,
   fbDevice: NULL,
   compositionType: 0,
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD_IF(HWC_DBG_DUMP_LAYER,"\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {

    // if there is no list or geometry is not changed,
    // there is no need to do any work here
    if( !list || (!(list->flags & HWC_GEOMETRY_CHANGED)))
        return 0;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);

    if (!hwcModule) {
        LOGE("hwc_prepare invalid module");
        return -1;
    }

    //TODO: this probably needs work
    LOGD_IF(HWC_DEBUG,"hwc_prepare found %d layers",list->numHwLayers);
    for (size_t i=0 ; i<list->numHwLayers ; i++) {
        dump_layer(&list->hwLayers[i]);

        // check for skip layer
        if (list->hwLayers[i].flags & HWC_SKIP_LAYER) {
            LOGD_IF(HWC_DEBUG,"hwc_prepare HWC_SKIP_LAYER on layer %d",i);
            ssize_t layer_countdown = ((ssize_t)i) - 1;
            // Mark every layer below the SKIP layer to be composed by the GPU
            while (layer_countdown >= 0)
            {
                list->hwLayers[layer_countdown].compositionType = HWC_FRAMEBUFFER;
                list->hwLayers[layer_countdown].hints &= ~HWC_HINT_CLEAR_FB;
                layer_countdown--;
            }
            continue;
        }



        // use copybit for everything
        if (hwcModule->compositionType & COMPOSITION_TYPE_MDP) {
            LOGD_IF(HWC_DEBUG,"hwc_prepare using copybit for layer %d", i);
            list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
        } else {
            LOGD_IF(HWC_DEBUG,"hwc_prepare copybit flag not set, using framebuffer for layer %d", i);
            list->hwLayers[i].compositionType = HWC_FRAMEBUFFER; //Google default
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
struct range {
    int current;
    int end;
};

struct region_iterator : public copybit_region_t {

    region_iterator(hwc_region_t region) {
        mRegion = region;
        r.end = region.numRects;
        r.current = 0;
        this->next = iterate;
    }

private:
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect) {

        if (!self) {
            LOGE("iterate invalid copybit region");
            return 0;
        } else if (!rect) {
            LOGE("iterate invalid copybit rect");
            return 0;
        }

        region_iterator const* me = static_cast<region_iterator const*>(self);
        if (me->r.current != me->r.end) {
            rect->l = me->mRegion.rects[me->r.current].left;
            rect->t = me->mRegion.rects[me->r.current].top;
            rect->r = me->mRegion.rects[me->r.current].right;
            rect->b = me->mRegion.rects[me->r.current].bottom;
            me->r.current++;
            return 1;
        }
        return 0;
    }

    hwc_region_t mRegion;
    mutable range r;
};

static int drawLayerUsingCopybit(hwc_composer_device_t *dev,
                                hwc_layer_t *layer,
                                EGLDisplay dpy,
                                EGLSurface surface)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         LOGE("drawLayerUsingCopybit null context ");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    if(!hwcModule) {
        LOGE("drawLayerUsingCopybit null module ");
        return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        LOGE("drawLayerUsingCopybit invalid handle");
        return -1;
    }

    // Lock this buffer for read.
    genlock_lock_type lockType = GENLOCK_READ_LOCK;
    int err = genlock_lock_buffer(hnd, lockType, GENLOCK_MAX_TIMEOUT);
    if (GENLOCK_FAILURE == err) {
        LOGE("drawLayerUsingCopybit genlock_lock_buffer(READ) failed");
        return -1;
    }
    //render buffer
    android_native_buffer_t *renderBuffer = (android_native_buffer_t *)eglGetRenderBufferANDROID(dpy, surface);
    if (!renderBuffer) {
        LOGE("drawLayerUsingCopybit eglGetRenderBufferANDROID returned NULL buffer");
        genlock_unlock_buffer(hnd);
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        LOGE("drawLayerUsingCopybit Framebuffer handle is NULL");
        genlock_unlock_buffer(hnd);
        return -1;
    }
    int alignment = 32;
    if( HAL_PIXEL_FORMAT_RGB_565 == fbHandle->format )
        alignment = 16;
     // Set the copybit source:
    copybit_image_t src;
    src.w = ALIGN(hnd->width, alignment);
    src.h = hnd->height;
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;
    src.horiz_padding = src.w - hnd->width;
    // Initialize vertical padding to zero for now,
    // this needs to change to accomodate vertical stride
    // if needed in the future
    src.vert_padding = 0;

    // Copybit source rect
    hwc_rect_t sourceCrop = layer->sourceCrop;
    copybit_rect_t srcRect = {sourceCrop.left, sourceCrop.top,
                              sourceCrop.right,
                              sourceCrop.bottom};

    // Copybit destination rect
    hwc_rect_t displayFrame = layer->displayFrame;
    copybit_rect_t dstRect = {displayFrame.left, displayFrame.top,
                              displayFrame.right,
                              displayFrame.bottom};

    // Copybit dst
    copybit_image_t dst;
    dst.w = ALIGN(fbHandle->width,alignment);
    dst.h = fbHandle->height;
    dst.format = fbHandle->format;
    dst.base = (void *)fbHandle->base;
    dst.handle = (native_handle_t *)renderBuffer->handle;

    copybit_device_t *copybit = hwcModule->copybitEngine;

    int32_t screen_w        = displayFrame.right - displayFrame.left;
    int32_t screen_h        = displayFrame.bottom - displayFrame.top;
    int32_t src_crop_width  = sourceCrop.right - sourceCrop.left;
    int32_t src_crop_height = sourceCrop.bottom -sourceCrop.top;

    int32_t copybitsMaxScale = copybit->get(copybit,COPYBIT_MAGNIFICATION_LIMIT);

    if(layer->transform & (HWC_TRANSFORM_ROT_90 | HWC_TRANSFORM_ROT_270)){
        //swap screen width and height
        int tmp = screen_w;
        screen_w  = screen_h;
        screen_h = tmp;
    }
    int32_t dsdx = screen_w/src_crop_width;
    int32_t dtdy = screen_h/src_crop_height;
    sp<GraphicBuffer> tempBitmap;

    if(dsdx  > copybitsMaxScale || dtdy > copybitsMaxScale){
        // The requested scale is out of the range the hardware
        // can support.
       LOGD("%s:%d::Need to scale dsdx=%d, dtdy=%d,maxScaleInv=%d,screen_w=%d,screen_h=%d \
                  src_crop_width=%d src_crop_height=%d",__FUNCTION__,__LINE__,
                  dsdx,dtdy,copybitsMaxScale,screen_w,screen_h,src_crop_width,src_crop_height);

       //Driver makes width and height as even
       //that may cause wrong calculation of the ratio
       //in display and crop.Hence we make
       //crop width and height as even.
       src_crop_width  = (src_crop_width/2)*2;
       src_crop_height = (src_crop_height/2)*2;

       int tmp_w =  src_crop_width*copybitsMaxScale;
       int tmp_h =  src_crop_height*copybitsMaxScale;

       LOGD("%s:%d::tmp_w = %d,tmp_h = %d",__FUNCTION__,__LINE__,tmp_w,tmp_h);
       tempBitmap = new GraphicBuffer(
                    tmp_w, tmp_h, src.format,
                    GraphicBuffer::USAGE_HW_2D);

       err = tempBitmap->initCheck();
       if (err == android::NO_ERROR){
            copybit_image_t tmp_dst;
            copybit_rect_t tmp_rect;
            tmp_dst.w = tmp_w;
            tmp_dst.h = tmp_h;
            tmp_dst.format = tempBitmap->format;
            tmp_dst.handle = (native_handle_t*)tempBitmap->getNativeBuffer()->handle;
            tmp_dst.horiz_padding = src.horiz_padding;
            tmp_dst.vert_padding = src.vert_padding;
            tmp_rect.l = 0;
            tmp_rect.t = 0;
            tmp_rect.r = tmp_dst.w;
            tmp_rect.b = tmp_dst.h;
            //create one clip region
            hwc_rect tmp_hwc_rect = {0,0,tmp_rect.r,tmp_rect.b};
            hwc_region_t tmp_hwc_reg = {1,(hwc_rect_t const*)&tmp_hwc_rect};
            region_iterator tmp_it(tmp_hwc_reg);
            copybit->set_parameter(copybit,COPYBIT_TRANSFORM,0);
            copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                        (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
            err = copybit->stretch(copybit,&tmp_dst, &src, &tmp_rect, &srcRect, &tmp_it);
            if(err < 0){
                LOGE("%s:%d::tmp copybit stretch failed",__FUNCTION__,__LINE__);
                return err;
            }
            // copy new src and src rect crop
            src = tmp_dst;
            srcRect = tmp_rect;
      }
    }

    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_WIDTH, renderBuffer->width);
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_HEIGHT, renderBuffer->height);
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, layer->transform);
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                           (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                           (layer->blending == HWC_BLENDING_PREMULT)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_DITHER,
                            (dst.format == HAL_PIXEL_FORMAT_RGB_565)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect, &copybitRegion);

    if(err < 0)
        LOGE("drawLayerUsingCopybit stretch failed");

    // Unlock this buffer since copybit is done with it.
    err = genlock_unlock_buffer(hnd);
    if (GENLOCK_FAILURE == err) {
        LOGE("drawLayerUsingCopybit genlock_unlock_buffer failed");
    }

    LOGD_IF(HWC_DEBUG_COPYBIT,"drawLayerUsingCopybit completed with err %d",err);
    return err;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{

    if (dpy == NULL && sur == NULL && list == NULL) {
        // special case: the screen is off, there is nothing to do.
        LOGD_IF(HWC_DEBUG,"hwc_set screen is off");
        return 0;
    } else if (!list) {
        // allow hwc_set to partially execute here, hack for screen off animation
        LOGD_IF(HWC_DEBUG,"hwc_set invalid list: attempting hack");
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            LOGE("hwc_set invalid list and eglSwapBuffers() failed");
            return HWC_EGL_ERROR;
        }
        return 0;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);

    if (!hwcModule) {
        LOGE("hwc_set invalid module");
        return -1;
    }

    for (size_t i=0; i<list->numHwLayers; i++) {
        if (list->hwLayers[i].flags & HWC_SKIP_LAYER) {
            continue;
        } else if (list->flags & HWC_SKIP_COMPOSITION) {
            continue;
        } else if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
            drawLayerUsingCopybit(dev, &(list->hwLayers[i]),
                                    (EGLDisplay)dpy, (EGLSurface)sur);
        }
    }

    if(list->flags & HWC_SKIP_COMPOSITION)
        LOGD_IF(HWC_DEBUG,"hwc_set skipping eglSwapBuffer call");

    // Do not call eglSwapBuffers if the skip composition flag is set on the list.
    if (!(list->flags & HWC_SKIP_COMPOSITION)) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            LOGE("hwc_set eglSwapBuffers() failed");
            return HWC_EGL_ERROR;
        }
    }

    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        LOGE("hwc_device_close null device pointer");
        return -1;
    }

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
            ctx->device.common.module);
    // Close the copybit module
    if(hwcModule->copybitEngine) {
        copybit_close(hwcModule->copybitEngine);
        hwcModule->copybitEngine = NULL;
    }
    if(hwcModule->fbDevice) {
        framebuffer_close(hwcModule->fbDevice);
        hwcModule->fbDevice = NULL;
    }

    if (ctx) {
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/
static int hwc_module_initialize(struct private_hwc_module_t* hwcModule)
{

    // Open the copybit module
    hw_module_t const *module;
    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &(hwcModule->copybitEngine));
    }
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(hwcModule->fbDevice));
    }

    // get the current composition type
    char property[PROPERTY_VALUE_MAX];
    if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            //debug.sf.hw = 0
            hwcModule->compositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            // Get the composition type
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_MDP;
            } else {
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            }

            if(!hwcModule->copybitEngine)
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
        }
    } else { //debug.sf.hw is not set. Use cpu composition
        hwcModule->compositionType = COMPOSITION_TYPE_CPU;
    }

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>
                                        (const_cast<hw_module_t*>(module));

        hwc_module_initialize(hwcModule);

        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;

        *device = &dev->device.common;
        status = 0;
    }
    return status;
}
