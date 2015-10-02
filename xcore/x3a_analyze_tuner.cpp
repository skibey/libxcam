/*
 * x3a_analyze_tuner.cpp - x3a analyzer Common IQ tuning adaptor
 *
 *  Copyright (c) 2014-2015 Intel Corporation
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

#include "xcam_utils.h"
#include "x3a_stats_pool.h"
#include "x3a_analyzer.h"
#include "x3a_analyzer_aiq.h"
#include "x3a_analyze_tuner.h"
#include "x3a_ciq_tuning_handler.h"
#include "x3a_ciq_tnr_tuning_handler.h"

namespace XCam {

X3aAnalyzeTuner::X3aAnalyzeTuner (struct atomisp_sensor_mode_data &sensor_data, const char *cpf_path)
    : X3aAnalyzer ("X3aAnalyzeTuner")
{
#if HAVE_IA_AIQ
    _aiq_analyzer = new X3aAnalyzerAiq (sensor_data, cpf_path);
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    _aiq_analyzer->set_results_callback (this);
    _aiq_analyzer->prepare_handlers ();
    _aiq_analyzer->set_sync_mode (true);
#endif
}

X3aAnalyzeTuner::~X3aAnalyzeTuner ()
{
}

XCamReturn
X3aAnalyzeTuner::create_tuning_handlers ()
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    SmartPtr<AeHandler> ae_handler = _aiq_analyzer->get_ae_handler ();
    SmartPtr<AwbHandler> awb_handler = _aiq_analyzer->get_awb_handler();

    SmartPtr<X3aCiqTuningHandler> tuning_handler = new X3aCiqTnrTuningHandler ();

    if (tuning_handler.ptr ()) {
        tuning_handler->set_ae_handler (ae_handler);
        tuning_handler->set_awb_handler (awb_handler);
        add_handler (tuning_handler);
    } else {
        ret = XCAM_RETURN_ERROR_PARAM;
    }

    return ret;
}

bool
X3aAnalyzeTuner::add_handler (SmartPtr<X3aCiqTuningHandler> &handler)
{
    XCAM_ASSERT (handler.ptr ());
    _handlers.push_back (handler);
    return true;
}

XCamReturn
X3aAnalyzeTuner::analyze_ae (XCamAeParam &param)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

#if HAVE_IA_AIQ
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    _aiq_analyzer->update_ae_parameters (param);
#endif
    return ret;
}

XCamReturn
X3aAnalyzeTuner::analyze_awb (XCamAwbParam &param)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

#if HAVE_IA_AIQ
    XCAM_ASSERT (_aiq_analyzer.ptr ());
#endif
    _aiq_analyzer->update_awb_parameters (param);
    return ret;
}

XCamReturn
X3aAnalyzeTuner::analyze_af (XCamAfParam &param)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

#if HAVE_IA_AIQ
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    _aiq_analyzer->update_af_parameters (param);
#endif
    return ret;
}

XCamReturn
X3aAnalyzeTuner::analyze_common (XCamCommonParam &param)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    XCAM_ASSERT (_aiq_analyzer.ptr ());
    _aiq_analyzer->update_common_parameters (param);
    return ret;
}

SmartPtr<AeHandler>
X3aAnalyzeTuner::create_ae_handler ()
{
    SmartPtr<AeHandler> ae_handler = new X3aCiqTuningAeHandler (this);
    return ae_handler;
}

SmartPtr<AwbHandler>
X3aAnalyzeTuner::create_awb_handler ()
{
    SmartPtr<AwbHandler> awb_handler = new X3aCiqTuningAwbHandler (this);
    return awb_handler;
}

SmartPtr<AfHandler>
X3aAnalyzeTuner::create_af_handler ()
{
    SmartPtr<AfHandler> af_handler = new X3aCiqTuningAfHandler (this);
    return af_handler;
}

SmartPtr<CommonHandler>
X3aAnalyzeTuner::create_common_handler ()
{
    SmartPtr<CommonHandler> common_handler = new X3aCiqTuningCommonHandler (this);
    return common_handler;
}

XCamReturn
X3aAnalyzeTuner::internal_init (uint32_t width, uint32_t height, double framerate)
{
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    _aiq_analyzer->init (width, height, framerate);

    if (XCAM_RETURN_NO_ERROR == ret) {
        ret = create_tuning_handlers ();
    }
    return ret;
}

XCamReturn
X3aAnalyzeTuner::internal_deinit ()
{
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    ret = _aiq_analyzer->deinit ();

    return ret;
}

XCamReturn
X3aAnalyzeTuner::configure_3a ()
{
    XCAM_ASSERT (_aiq_analyzer.ptr ());
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    ret = _aiq_analyzer->start ();

    return ret;
}

XCamReturn
X3aAnalyzeTuner::pre_3a_analyze (SmartPtr<X3aStats> &stats)
{
    // save stats for aiq analyzer
    XCamReturn ret = XCAM_RETURN_NO_ERROR;
    if (stats.ptr ()) {
        _stats = stats;
    }
    return ret;
}

XCamReturn
X3aAnalyzeTuner::post_3a_analyze (X3aResultList &results)
{
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    XCAM_ASSERT (_aiq_analyzer.ptr ());
    ret = _aiq_analyzer->push_3a_stats (_stats);
    _stats.release ();

    results.insert (results.end (), _results.begin (), _results.end ());
    _results.clear ();

    X3aCiqTuningHandlerList::iterator i_handler = _handlers.begin ();
    for (; i_handler != _handlers.end ();  ++i_handler)
    {
        (*i_handler)->analyze (results);
    }

    return ret;
}

void
X3aAnalyzeTuner::x3a_calculation_done (XAnalyzer *analyzer, X3aResultList &results)
{
    XCAM_UNUSED (analyzer);
    _results.clear ();
    _results.assign (results.begin (), results.end ());
}

};

