/*
 * Copyright 2018 KEDACOM Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hlsservice.h"

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(my_category);
#define GST_CAT_DEFAULT my_category

HLSService::HLSService(IApp *app, const std::string &name)
    : IEndpoint(app, name)
    , hlssink2_(NULL)
    , hlssink2_video_(NULL)
    , hlssink2_audio_(NULL)
    , pipeline_(NULL)
{
}

HLSService::~HLSService()
{
}

bool HLSService::initialize(Promise *promise)
{
    GST_DEBUG_CATEGORY_INIT(my_category, "webstreamer", 2, "libWebStreamer");
    IEndpoint::protocol() = "hlsservice";
    
    hlssink2_ = gst_element_factory_make("hlssink2", "hlssink");
    //gst_util_set_object_arg(G_OBJECT(hlssink2_), "cache-mode", "memory");
    //g_object_set(G_OBJECT(hlssink2_), "cache-mode", 1, NULL);
    const json &j = promise->data();
    //set available properties for hlssink2_
    if ( j.find("location") != j.end() ) {
        const std::string &location = j["location"];
        g_object_set(G_OBJECT(hlssink2_), "location", location.c_str(), NULL);
        GST_DEBUG("[hlsservice: %s] hlssink2(%p) with location (%s)", name().c_str(), hlssink2_,  location.c_str());
    }
    if ( j.find("target-duration") != j.end() ) {
        g_object_set(G_OBJECT(hlssink2_), "target-duration", j["target-duration"], NULL);
    }
    if ( j.find("playlist-location") != j.end() ) {
        g_object_set(G_OBJECT(hlssink2_), "playlist-location", j["playlist-location"], NULL);
    }
    if ( j.find("playlist-length") != j.end() ) {
        g_object_set(G_OBJECT(hlssink2_), "playlist-length", j["playlist-length"], NULL);
    }
    
    pipeline_ = gst_pipeline_new(NULL);
    g_warn_if_fail( gst_bin_add(GST_BIN(pipeline_), hlssink2_) );
    //link pipeline_ to app's
    if (!app()->video_encoding().empty()) {
        std::string media_type = "video";
        video_joint_ = make_pipe_joint(media_type);

        app()->add_pipe_joint( video_joint_.upstream_joint );
        g_warn_if_fail( gst_bin_add(GST_BIN(pipeline_), video_joint_.downstream_joint) );

        GstPad * srcpad = gst_element_get_static_pad(video_joint_.downstream_joint, "src");
        GstPadTemplate * templ = gst_element_get_pad_template(hlssink2_, "video");
        hlssink2_video_ = gst_element_request_pad( hlssink2_, templ, NULL, NULL );
        g_warn_if_fail( gst_pad_link(srcpad, hlssink2_video_) == GST_PAD_LINK_OK );
    }
    
    if (!app()->audio_encoding().empty()) {
        std::string media_type = "audio";
        audio_joint_ = make_pipe_joint(media_type);

        app()->add_pipe_joint( audio_joint_.upstream_joint );
        g_warn_if_fail( gst_bin_add(GST_BIN(pipeline_), audio_joint_.downstream_joint) );

        GstPad * srcpad = gst_element_get_static_pad(audio_joint_.downstream_joint, "src");
        GstPadTemplate * templ = gst_element_get_pad_template(hlssink2_, "audio");
        hlssink2_audio_ = gst_element_request_pad( hlssink2_, templ, NULL, NULL );
        g_warn_if_fail( gst_pad_link(srcpad, hlssink2_audio_) == GST_PAD_LINK_OK );
    }
    
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        GST_DEBUG("[hlsservice: %s] %p initialize failed.", name().c_str(), hlssink2_);
        return false;
    }
    GST_DEBUG("[hlsservice: %s] %p initialize done.", name().c_str(), hlssink2_);

    return true;
}

void HLSService::terminate()
{
    if (!app()->video_encoding().empty() 
        && video_joint_.upstream_joint != NULL) {
        app()->remove_pipe_joint(video_joint_.upstream_joint);
    }
    if (!app()->audio_encoding().empty() 
        && audio_joint_.upstream_joint != NULL) {
        app()->remove_pipe_joint(audio_joint_.upstream_joint);
    }

    if(hlssink2_video_ != NULL) {
        gst_element_release_request_pad(hlssink2_, hlssink2_video_);
        hlssink2_video_ = NULL;
    }

    if(hlssink2_audio_ != NULL) {
        gst_element_release_request_pad(hlssink2_, hlssink2_audio_);
        hlssink2_audio_ = NULL;
    }

    if (pipeline_) {
        gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
        gst_bin_remove(GST_BIN(pipeline_), hlssink2_);
        gst_object_unref(pipeline_);
        hlssink2_ = NULL;
        pipeline_ = NULL;
    }

    GST_DEBUG("[hlsservice: %s] terminate done.", name().c_str());
}