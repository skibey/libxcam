/*
 * cl_yuv_pipe_handler.cpp - CL YuvPipe Pipe handler
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
 * Author: Wangfei <feix.w.wang@intel.com>
 */
#include "xcam_utils.h"
#include "cl_yuv_pipe_handler.h"
float default_matrix[XCAM_COLOR_MATRIX_SIZE] = {0.299, 0.587, 0.114, -0.14713, -0.28886, 0.436, 0.615, -0.51499, -0.10001};
float default_macc[XCAM_CHROMA_AXIS_SIZE*XCAM_CHROMA_MATRIX_SIZE] = {
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000,
    1.000000, 0.000000, 0.000000, 1.000000, 1.000000, 0.000000, 0.000000, 1.000000
};

namespace XCam {

CLYuvPipeImageKernel::CLYuvPipeImageKernel (SmartPtr<CLContext> &context)
    : CLImageKernel (context, "kernel_yuv_pipe")
    , _vertical_offset (0)
    , _gain_rgb (0.0)
    , _gain_yuv (1.0)
    , _thr_r (0.064)  // set high initial threshold to get strong denoise effect
    , _thr_g (0.045)
    , _thr_b (0.073)
    , _thr_y (0.05)
    , _thr_uv (0.05)
    , _framecount (2)
    , _enable_tnr_rgb (0)
    , _enable_tnr_yuv (0)
{
    memcpy(_macc_table, default_macc, sizeof(float)*XCAM_CHROMA_AXIS_SIZE * XCAM_CHROMA_MATRIX_SIZE);
    memcpy(_rgbtoyuv_matrix, default_matrix, sizeof(float)*XCAM_COLOR_MATRIX_SIZE);
}

bool
CLYuvPipeImageKernel::set_macc (const XCam3aResultMaccMatrix &macc)
{
    for(int i = 0; i < XCAM_CHROMA_AXIS_SIZE * XCAM_CHROMA_MATRIX_SIZE; i++)
        _macc_table[i] = (float)macc.table[i];
    return true;
}

bool
CLYuvPipeImageKernel::set_matrix (const XCam3aResultColorMatrix &matrix)
{
    for (int i = 0; i < XCAM_COLOR_MATRIX_SIZE; i++)
        _rgbtoyuv_matrix[i] = (float)matrix.matrix[i];
    return true;
}

bool
CLYuvPipeImageKernel::set_tnr_rgb_config (const XCam3aResultTemporalNoiseReduction& config)
{
    _gain_rgb = (float)config.gain;
    _thr_r  = (float)config.threshold[0];
    _thr_g  = (float)config.threshold[1];
    _thr_b  = (float)config.threshold[2];
    XCAM_LOG_DEBUG ("set YUV-Pipe tnr rgb config: _gain(%f), _thr_r(%f), _thr_g(%f), _thr_b(%f)",
                    _gain_rgb, _thr_r, _thr_g, _thr_b);

    return true;
}

bool
CLYuvPipeImageKernel::set_tnr_yuv_config (const XCam3aResultTemporalNoiseReduction& config)
{
    _gain_yuv = (float)config.gain;
    _thr_y = (float)config.threshold[0];
    _thr_uv = (float)config.threshold[1];
    XCAM_LOG_DEBUG ("set TNR YUV config: _gain(%f), _thr_y(%f), _thr_uv(%f)",
                    _gain_yuv, _thr_y, _thr_uv);

    return true;
}

bool
CLYuvPipeImageKernel::set_tnr_enable (bool enable_tnr_rgb, bool enable_tnr_yuv)
{
    _enable_tnr_rgb = (enable_tnr_rgb ? 1 : 0);
    _enable_tnr_yuv = (enable_tnr_yuv ? 1 : 0);
    return true;
}

XCamReturn
CLYuvPipeImageKernel::prepare_arguments (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output,
    CLArgument args[], uint32_t &arg_count,
    CLWorkSize &work_size)
{
    SmartPtr<CLContext> context = get_context ();
    const VideoBufferInfo & video_info = output->get_video_info ();

    _image_in = new CLVaImage (context, input);
    _image_out = new CLVaImage (context, output);
    _matrix_buffer = new CLBuffer (
        context, sizeof(float)*XCAM_COLOR_MATRIX_SIZE,
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR , &_rgbtoyuv_matrix);
    _macc_table_buffer = new CLBuffer(
        context, sizeof(float)*XCAM_CHROMA_AXIS_SIZE * XCAM_CHROMA_MATRIX_SIZE,
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR , &_macc_table);

    if (_image_in_list.size () < 4) {
        while (_image_in_list.size () < 4) {
            _image_in_list.push_back (_image_in);
        }
    } else {
        _image_in_list.pop_front ();
        _image_in_list.push_back (_image_in);
    }

    _vertical_offset = video_info.aligned_height;

    if (!_image_out_prev.ptr ()) {
        _image_out_prev = _image_out;
        _enable_tnr_yuv_state = _enable_tnr_yuv;
        _enable_tnr_yuv = 0;
    }
    else {
        if (_enable_tnr_yuv == 0)
            _enable_tnr_yuv = _enable_tnr_yuv_state;
    }
    XCAM_ASSERT (_image_in->is_valid () && _image_out->is_valid ());
    XCAM_FAIL_RETURN (
        WARNING,
        _image_in->is_valid () && _image_out->is_valid (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) in/out memory not available", get_kernel_name ());

    //set args;
    args[0].arg_adress = &_image_out->get_mem_id ();
    args[0].arg_size = sizeof (cl_mem);
    args[1].arg_adress = &_image_out_prev->get_mem_id ();
    args[1].arg_size = sizeof (cl_mem);
    args[2].arg_adress = &_vertical_offset;
    args[2].arg_size = sizeof (_vertical_offset);
    args[3].arg_adress = &_matrix_buffer->get_mem_id();
    args[3].arg_size = sizeof (cl_mem);
    args[4].arg_adress = &_macc_table_buffer->get_mem_id();
    args[4].arg_size = sizeof (cl_mem);
    args[5].arg_adress = &_framecount;
    args[5].arg_size = sizeof (_framecount);
    args[6].arg_adress = &_gain_rgb;
    args[6].arg_size = sizeof (_gain_rgb);
    args[7].arg_adress = &_thr_r;
    args[7].arg_size = sizeof (_thr_r);
    args[8].arg_adress = &_thr_g;
    args[8].arg_size = sizeof (_thr_g);
    args[9].arg_adress = &_thr_b;
    args[9].arg_size = sizeof (_thr_b);
    args[10].arg_adress = &_gain_yuv;
    args[10].arg_size = sizeof (_gain_yuv);
    args[11].arg_adress = &_thr_y;
    args[11].arg_size = sizeof (_thr_y);
    args[12].arg_adress = &_thr_uv;
    args[12].arg_size = sizeof (_thr_uv);
    args[13].arg_adress = &_enable_tnr_rgb;
    args[13].arg_size = sizeof (_enable_tnr_rgb);
    args[14].arg_adress = &_enable_tnr_yuv;
    args[14].arg_size = sizeof (_enable_tnr_yuv);

    uint8_t index = 0;
    for (std::list<SmartPtr<CLImage>>::iterator it = _image_in_list.begin (); it != _image_in_list.end (); it++) {
        args[15 + index].arg_adress = &(*it)->get_mem_id ();
        args[15 + index].arg_size = sizeof (cl_mem);
        index++;
    }

    arg_count = 15 + index;

    work_size.dim = XCAM_DEFAULT_IMAGE_DIM;
    work_size.global[0] = video_info.width / 2 ;
    work_size.global[1] = video_info.aligned_height / 2 ;
    work_size.local[0] = 4;
    work_size.local[1] = 4;

    return XCAM_RETURN_NO_ERROR;
}

XCamReturn
CLYuvPipeImageKernel::post_execute ()
{
    if (_image_out->is_valid ()) {
        _image_out_prev = _image_out;
    }
    _image_in.release ();
    _image_out.release ();
    _matrix_buffer.release ();
    _macc_table_buffer.release ();

    return XCAM_RETURN_NO_ERROR;
}

CLYuvPipeImageHandler::CLYuvPipeImageHandler (const char *name)
    : CLImageHandler (name)
    , _output_format(V4L2_PIX_FMT_NV12)
{
}

bool
CLYuvPipeImageHandler::set_macc_table (const XCam3aResultMaccMatrix &macc)
{
    return _yuv_pipe_kernel->set_macc (macc);
}

bool
CLYuvPipeImageHandler::set_rgbtoyuv_matrix (const XCam3aResultColorMatrix &matrix)
{
    return _yuv_pipe_kernel->set_matrix (matrix);
}

XCamReturn
CLYuvPipeImageHandler::prepare_buffer_pool_video_info (
    const VideoBufferInfo &input,
    VideoBufferInfo &output)
{
    bool format_inited = output.init (_output_format, input.width, input.height);

    XCAM_FAIL_RETURN (
        WARNING,
        format_inited,
        XCAM_RETURN_ERROR_PARAM,
        "CL image handler(%s) ouput format(%s) unsupported",
        get_name (), xcam_fourcc_to_string (_output_format));

    return XCAM_RETURN_NO_ERROR;
}

bool
CLYuvPipeImageHandler::set_yuv_pipe_kernel(SmartPtr<CLYuvPipeImageKernel> &kernel)
{
    SmartPtr<CLImageKernel> image_kernel = kernel;
    add_kernel (image_kernel);
    _yuv_pipe_kernel = kernel;
    return true;
}

bool
CLYuvPipeImageHandler::set_tnr_rgb_config (const XCam3aResultTemporalNoiseReduction& config)
{
    if (!_yuv_pipe_kernel->is_valid ()) {
        XCAM_LOG_ERROR ("set config error, invalid YUV-Pipe kernel !");
    }

    _yuv_pipe_kernel->set_tnr_rgb_config (config);

    return true;
}

bool
CLYuvPipeImageHandler::set_tnr_yuv_config (const XCam3aResultTemporalNoiseReduction& config)
{
    if (!_yuv_pipe_kernel->is_valid ()) {
        XCAM_LOG_ERROR ("set config error, invalid YUV-Pipe kernel !");
    }

    _yuv_pipe_kernel->set_tnr_yuv_config (config);

    return true;
}

bool
CLYuvPipeImageHandler::set_tnr_enable (bool enable_tnr_rgb, bool enable_tnr_yuv)
{
    return _yuv_pipe_kernel->set_tnr_enable (enable_tnr_rgb, enable_tnr_yuv);
}

SmartPtr<CLImageHandler>
create_cl_yuv_pipe_image_handler (SmartPtr<CLContext> &context)
{
    SmartPtr<CLYuvPipeImageHandler> yuv_pipe_handler;
    SmartPtr<CLYuvPipeImageKernel> yuv_pipe_kernel;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    yuv_pipe_kernel = new CLYuvPipeImageKernel (context);
    {
        XCAM_CL_KERNEL_FUNC_SOURCE_BEGIN(kernel_yuv_pipe)
#include "kernel_yuv_pipe.clx"
        XCAM_CL_KERNEL_FUNC_END;
        ret = yuv_pipe_kernel->load_from_source (kernel_yuv_pipe_body, strlen (kernel_yuv_pipe_body));
        XCAM_FAIL_RETURN (
            WARNING,
            ret == XCAM_RETURN_NO_ERROR,
            NULL,
            "CL image handler(%s) load source failed", yuv_pipe_kernel->get_kernel_name());
    }
    XCAM_ASSERT (yuv_pipe_kernel->is_valid ());
    yuv_pipe_handler = new CLYuvPipeImageHandler ("cl_handler_pipe_yuv");
    yuv_pipe_handler->set_yuv_pipe_kernel (yuv_pipe_kernel);

    return yuv_pipe_handler;
}

};
