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

#include "filesource.h"
#include <utils/typedef.h>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(my_category);
#define GST_CAT_DEFAULT my_category

FileSource::FileSource(IApp *app, const std::string &name)
    : IEndpoint(app, name)
    , filesrc_(NULL)
    , demux_(NULL)
    , video_queue_(NULL)
    , audio_queue_(NULL)
    , video_parse_(NULL)
    , audio_parse_(NULL)
    , add_to_pipeline_(false)
{
}

FileSource::~FileSource()
{
}

bool FileSource::initialize(Promise *promise)
{
    GST_DEBUG_CATEGORY_INIT(my_category, "webstreamer", 2, "libWebStreamer");
    
    const json &j = promise->data();
    const std::string &url = j["url"];
    GST_DEBUG("[filesource : %s] source url: %s", name().c_str(), url.c_str());
    IEndpoint::protocol() = "filesource";

    filesrc_ = gst_element_factory_make("filesrc", "filesrc");
    g_warn_if_fail(filesrc_);
    g_object_set(G_OBJECT(filesrc_), "location", url.c_str(), NULL);

    if ( j.find("video_codec") != j.end() ) {
        app()->video_encoding() = j["video_codec"];
    }
    if (j.find("audio_codec") != j.end()) {
        app()->audio_encoding() = j["audio_codec"];
    }

    return add_to_pipeline();
}

void FileSource::on_demux_pad_added(GstElement* demux, GstPad* src_pad, gpointer filesource)
{
    FileSource *file_source = static_cast<FileSource *>(filesource);
    GstCaps *caps = gst_pad_query_caps(src_pad, NULL);
    GstStructure *stru = gst_caps_get_structure(caps, 0);
    const GValue *media_type = gst_structure_get_value(stru, "media");

    auto pipeline = file_source->app();

    if (g_str_equal(g_value_get_string(media_type), "video")) {
        if (!pipeline->video_encoding().empty()) {
            GstPad *sink_pad = gst_element_get_static_pad(file_source->video_queue_, "sink");
            g_warn_if_fail( gst_pad_link(src_pad, sink_pad) == GST_PAD_LINK_OK );
            gst_object_unref(sink_pad);
            GST_DEBUG("[filesource :%s] video demux-queue link", file_source->name().c_str());
        }
    } 
    else if (g_str_equal(g_value_get_string(media_type), "audio")) {
        if (!pipeline->audio_encoding().empty()) {
            GstPad *sink_pad = gst_element_get_static_pad(file_source->audio_queue_, "sink");
            g_warn_if_fail( gst_pad_link(src_pad, sink_pad) == GST_PAD_LINK_OK );
            gst_object_unref(sink_pad);
             GST_DEBUG("[filesource :%s] audio demux-queue link", file_source->name().c_str());
        }
    } 
    else {
        g_warn_if_reached();
    }
}

bool FileSource::add_to_pipeline()
{
    if ( !add_to_pipeline_ ) {
        demux_ = gst_element_factory_make("qtdemux", "demux");
        gst_bin_add_many(GST_BIN(app()->pipeline()), filesrc_, demux_, NULL);
        g_warn_if_fail(gst_element_link(filesrc_, demux_));
        g_signal_connect(demux_, "pad-added", (GCallback)on_demux_pad_added, this);
        add_to_pipeline_ = true;
    }
    
    if ( !app()->video_encoding().empty() ) {
        VideoEncodingType video_codec = get_video_encoding_type(app()->video_encoding());
        switch(video_codec) {
            case VideoEncodingType::H264:
            {
                video_queue_ = gst_element_factory_make("queue", "video_queue");
                video_parse_ = gst_element_factory_make("h264parse", "video_parse");
            }
            break;

            default:
            GST_ERROR("[ filesource ] invalid Video Codec!");
            return false;
        }
        g_return_val_if_fail(video_queue_!=NULL && video_parse_!=NULL, false);
        gst_bin_add_many(GST_BIN(app()->pipeline()), video_queue_, video_parse_, NULL);
        g_warn_if_fail(gst_element_link(video_queue_, video_parse_));
        GST_DEBUG("[filesource] configured video: %s", app()->video_encoding().c_str());
    }

    if( !app()->audio_encoding().empty() ) {
        AudioEncodingType audio_codec = get_audio_encoding_type(app()->audio_encoding());
        switch(audio_codec) {
            case AudioEncodingType::AAC:
            {
                audio_queue_ = gst_element_factory_make("queue", "audio_queue");
                audio_parse_ = gst_element_factory_make("aacparse", "audio_parse");
            }
            break;

            default:
            GST_ERROR("[filesource] invalid Audio Codec!");
            return false;
        }
        
        g_return_val_if_fail(audio_queue_!=NULL && audio_parse_!=NULL, false);
        gst_bin_add_many(GST_BIN(app()->pipeline()), audio_queue_, audio_parse_, NULL);
        g_warn_if_fail(gst_element_link(audio_queue_, audio_parse_));
        GST_DEBUG("[filesource] configured audio: %s", app()->audio_encoding().c_str());
    }
    GST_DEBUG("[filesource: %s] initialize done.", name().c_str());

    return true;
}

void FileSource::terminate()
{
    gst_bin_remove_many(GST_BIN(app()->pipeline()), filesrc_, demux_, NULL);
    gst_bin_remove_many(GST_BIN(app()->pipeline()), video_queue_, video_parse_, NULL);
    gst_bin_remove_many(GST_BIN(app()->pipeline()), audio_queue_, audio_parse_, NULL);
    GST_DEBUG("[filesource: %s] terminate done.", name().c_str());
}