/*
 * sample_smart_analysis.cpp - smart analysis sample code
 *
 *  Copyright (c) 2015 Intel Corporation
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
 *
 * Author: Zong Wei <wei.zong@intel.com>
 */

#include <base/xcam_smart_description.h>
#include "xcam_utils.h"
#include "aiq3a_utils.h"
#include "buffer_pool.h"
#include "x3a_result_factory.h"
#include "smart_analyzer.h"

using namespace XCam;

#define DEFAULT_SAVE_FRAME_NAME "frame_buffer"
#define XSMART_ANALYSIS_CONTEXT_CAST(context)  ((XCamSmartAnalyerContext*)(context))

class FrameSaver
{
public:
    explicit FrameSaver (bool save, uint32_t interval, uint32_t count);
    ~FrameSaver ();

    void save_frame (XCamVideoBuffer *buffer);

    void enable_save_file (bool enable) {
        _save_file = enable;
    }
    void set_interval (uint32_t inteval) {
        _interval = inteval;
    }
    void set_frame_save (uint32_t frame_save) {
        _frame_save = frame_save;
    }

private:
    XCAM_DEAD_COPY (FrameSaver);
    void open_file ();
    void close_file ();

private:
    FILE *_file;
    bool _save_file;
    uint32_t _interval;
    uint32_t _frame_save;
    uint32_t _frame_count;

};

FrameSaver::FrameSaver (bool save, uint32_t interval, uint32_t count)
    : _file (NULL)
    , _save_file (save)
    , _interval (interval)
    , _frame_save (count)
    , _frame_count (0)
{
}

void
FrameSaver::save_frame (XCamVideoBuffer *buffer)
{
    if (NULL == buffer) {
        return;
    }
    if (!_save_file)
        return ;

    if ((_frame_count++ % _interval) != 0)
        return;

    if (_frame_count > (_frame_save * _interval)) {
        return;
    }

    open_file ();

    if (!_file) {
        XCAM_LOG_ERROR ("open file failed");
        return;
    }

    if (fwrite (buffer->data, buffer->info.size, 1, _file) <= 0) {
        XCAM_LOG_WARNING ("write frame failed.");
    }
    close_file ();
}

void
FrameSaver::open_file ()
{
    if ((_file) && (_frame_save == 0))
        return;

    char file_name[512];
    if (_frame_save != 0) {
        snprintf (file_name, sizeof(file_name), "%s%d%s", DEFAULT_SAVE_FRAME_NAME, _frame_count, ".yuv");
    }

    _file = fopen(file_name, "wb");
}

void
FrameSaver::close_file ()
{
    if (_file)
        fclose (_file);
    _file = NULL;
}

class SampleHandler
{
public:
    explicit SampleHandler (const char *name = NULL);
    virtual ~SampleHandler ();

    XCamReturn init (uint32_t width, uint32_t height, double framerate);
    XCamReturn deinit ();
    bool set_results_callback (AnalyzerCallback *callback);

    XCamReturn update_params (XCamSmartAnalysisParam *params);
    XCamReturn analyze (XCamVideoBuffer *buffer);

private:
    XCAM_DEAD_COPY (SampleHandler);

private:
    char                    *_name;
    uint32_t                 _width;
    uint32_t                 _height;
    double                   _framerate;
    AnalyzerCallback        *_callback;
    SmartPtr<FrameSaver>    _frameSaver;
};

SampleHandler::SampleHandler (const char *name)
{
    if (name)
        _name = strdup (name);

    if (!_frameSaver.ptr ()) {
        _frameSaver = new FrameSaver (true, 20, 10);
    }
}

SampleHandler::~SampleHandler ()
{
}

XCamReturn
SampleHandler::init (uint32_t width, uint32_t height, double framerate)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    _width = width;
    _height = height;
    _framerate = framerate;

    return ret;
}

XCamReturn
SampleHandler::deinit ()
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    return ret;
}

bool
SampleHandler::set_results_callback (AnalyzerCallback *callback)
{
    XCAM_ASSERT (!_callback);
    _callback = callback;
    return true;
}

XCamReturn
SampleHandler::update_params (XCamSmartAnalysisParam *params)
{
    XCAM_UNUSED (params);

    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    return ret;
}

XCamReturn
SampleHandler::analyze (XCamVideoBuffer *buffer)
{
    XCAM_LOG_DEBUG ("Smart SampleHandler::analyze on ts:" XCAM_TIMESTAMP_FORMAT, XCAM_TIMESTAMP_ARGS (buffer->timestamp));
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    XCamVideoBufferInfo info = buffer->info;

    XCAM_LOG_DEBUG ("format(0x%x), color_bits(%d)", info.format, info.color_bits);
    XCAM_LOG_DEBUG ("size(%d), components(%d)", info.size, info.components);
    XCAM_LOG_DEBUG ("width(%d), heitht(%d)", info.width, info.height);
    XCAM_LOG_DEBUG ("aligned_width(%d), aligned_height(%d)", info.aligned_width, info.aligned_height);

    _frameSaver->save_frame (buffer);

    X3aResultList results;
    XCam3aResultBrightness *xcam3a_brightness_result = xcam_malloc0_type (XCam3aResultBrightness);
    xcam3a_brightness_result->head.type =   XCAM_3A_RESULT_BRIGHTNESS;
    xcam3a_brightness_result->head.process_type = XCAM_IMAGE_PROCESS_ALWAYS;
    xcam3a_brightness_result->head.version = XCAM_VERSION;
    xcam3a_brightness_result->brightness_level = 9.9;

    SmartPtr<X3aResult> brightness_result =
        X3aResultFactory::instance ()->create_3a_result ((XCam3aResultHead*)xcam3a_brightness_result);
    results.push_back(brightness_result);

    if (_callback) {
        if (XCAM_RETURN_NO_ERROR == ret) {
            _callback->x3a_calculation_done (NULL, results);
        } else {
            _callback->x3a_calculation_failed (NULL, buffer->timestamp, "pre 3a analyze failed");
        }
    }

    return ret;
}

class XCamSmartAnalyerContext
    : public AnalyzerCallback
{
public:
    XCamSmartAnalyerContext ();
    ~XCamSmartAnalyerContext ();
    bool setup_handler ();
    SmartPtr<SampleHandler> &get_handler () {
        return _handler;
    }

    uint32_t get_results (X3aResultList &results);

    // derive from AnalyzerCallback
    virtual void x3a_calculation_done (XAnalyzer *analyzer, X3aResultList &results);

private:
    XCAM_DEAD_COPY (XCamSmartAnalyerContext);

private:
// members
    SmartPtr<SampleHandler> _handler;
    SmartPtr<BufferPool>    _buffer_pool;
    Mutex                   _result_mutex;
    X3aResultList           _results;
};

XCamSmartAnalyerContext::XCamSmartAnalyerContext ()
{
    setup_handler ();
}

XCamSmartAnalyerContext::~XCamSmartAnalyerContext ()
{
    _handler->deinit ();
}

bool
XCamSmartAnalyerContext::setup_handler ()
{
    XCAM_ASSERT (!_handler.ptr ());
    _handler = new SampleHandler ();
    XCAM_ASSERT (_handler.ptr ());
    _handler->set_results_callback (this);
    return true;
}

void
XCamSmartAnalyerContext::x3a_calculation_done (XAnalyzer *analyzer, X3aResultList &results)
{
    XCAM_UNUSED (analyzer);
    SmartLock  locker (_result_mutex);
    _results.insert (_results.end (), results.begin (), results.end ());
}

uint32_t
XCamSmartAnalyerContext::get_results (X3aResultList &results)
{
    uint32_t size = 0;
    SmartLock  locker (_result_mutex);

    results.assign (_results.begin (), _results.end ());
    size = _results.size ();
    _results.clear ();

    return size;
}

static XCamReturn
xcam_create_context (XCamSmartAnalysisContext **context)
{
    XCAM_ASSERT (context);
    XCamSmartAnalyerContext *analysis_context = new XCamSmartAnalyerContext ();
    *context = ((XCamSmartAnalysisContext*)(analysis_context));

    return XCAM_RETURN_NO_ERROR;
}

static XCamReturn
xcam_destroy_context (XCamSmartAnalysisContext *context)
{
    XCamSmartAnalyerContext *analysis_context = XSMART_ANALYSIS_CONTEXT_CAST (context);
    if (analysis_context)
        delete analysis_context;
    return XCAM_RETURN_NO_ERROR;
}

static XCamReturn
xcam_update_params (XCamSmartAnalysisContext *context, XCamSmartAnalysisParam *params)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    XCamSmartAnalyerContext *analysis_context = XSMART_ANALYSIS_CONTEXT_CAST (context);
    XCAM_ASSERT (analysis_context);

    SmartPtr<SampleHandler> handler = analysis_context->get_handler ();
    XCAM_ASSERT (handler.ptr ());
    XCAM_ASSERT (params);

    ret = handler->update_params (params);
    if (ret != XCAM_RETURN_NO_ERROR) {
        XCAM_LOG_WARNING ("update params failed");
    }

    return ret;
}

static XCamReturn
xcam_analyze (XCamSmartAnalysisContext *context, XCamVideoBuffer *buffer)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    if (!buffer) {
        return XCAM_RETURN_ERROR_PARAM;
    }

    XCamSmartAnalyerContext *analysis_context = XSMART_ANALYSIS_CONTEXT_CAST (context);
    XCAM_ASSERT (analysis_context);

    SmartPtr<SampleHandler> handler = analysis_context->get_handler ();
    XCAM_ASSERT (handler.ptr ());

    ret = handler->analyze(buffer);
    if (ret != XCAM_RETURN_NO_ERROR) {
        XCAM_LOG_WARNING ("buffer analyze failed");
    }

    return ret;
}

static XCamReturn
xcam_get_results (XCamSmartAnalysisContext *context, XCam3aResultHead *results[], uint32_t *res_count)
{
    XCamSmartAnalyerContext *analysis_context = XSMART_ANALYSIS_CONTEXT_CAST (context);
    XCAM_ASSERT (analysis_context);
    X3aResultList analysis_results;
    uint32_t result_count = analysis_context->get_results (analysis_results);

    if (!result_count) {
        *res_count = 0;
        XCAM_LOG_DEBUG ("Smart Analysis return no result");
        return XCAM_RETURN_NO_ERROR;
    }

    // mark as static
    static XCam3aResultHead *res_array[XCAM_3A_MAX_RESULT_COUNT];
    xcam_mem_clear (res_array);
    XCAM_ASSERT (result_count < XCAM_3A_MAX_RESULT_COUNT);
    result_count = translate_3a_results_to_xcam (analysis_results, res_array, XCAM_3A_MAX_RESULT_COUNT);

    for (uint32_t i = 0; i < result_count; ++i) {
        results[i] = res_array[i];
    }
    *res_count = result_count;
    XCAM_ASSERT (result_count > 0);

    return XCAM_RETURN_NO_ERROR;
}

static void
xcam_free_results (XCam3aResultHead *results[], uint32_t res_count)
{
    for (uint32_t i = 0; i < res_count; ++i) {
        if (results[i])
            free_3a_result (results[i]);
    }
}

XCAM_BEGIN_DECLARE

XCamSmartAnalysisDescription xcam_smart_analysis_desciption = {
    XCAM_VERSION,
    sizeof (XCamSmartAnalysisDescription),
    xcam_create_context,
    xcam_destroy_context,
    xcam_update_params,
    xcam_analyze,
    xcam_get_results,
    xcam_free_results
};

XCAM_END_DECLARE

