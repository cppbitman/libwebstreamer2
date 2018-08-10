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

#include "hlstream.h"
#include <endpoint/filesource.h>
#include <endpoint/hlsservice.h>
#include <utils/typedef.h>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(my_category);
#define GST_CAT_DEFAULT my_category

#define HLSTREAM_JOINT_LOCK(hls)    (((hls)->joint_mutex_).lock())
#define HLSTREAM_JOINT_UNLOCK(hls)  (((hls)->joint_mutex_).unlock())

HLStream::HLStream(const std::string &name, WebStreamer *ws)
    : IApp(name, ws)
    , performer_(NULL)
    , fake_pad_video_tee_(NULL)
    , fake_pad_audio_tee_(NULL)
{
}

HLStream::~HLStream()
{
}

bool HLStream::Initialize(Promise *promise)
{
    IApp::Initialize(promise);
    video_tee_ = gst_element_factory_make("tee", "video_tee");
    audio_tee_ = gst_element_factory_make("tee", "audio_tee");

    g_warn_if_fail(gst_bin_add(GST_BIN(pipeline()), video_tee_));
    g_warn_if_fail(gst_bin_add(GST_BIN(pipeline()), audio_tee_));

    GST_DEBUG_CATEGORY_INIT(my_category, "webstreamer", 2, "libWebStreamer");

    return true;
}

bool HLStream::Destroy(Promise *promise)
{ 
    bool ret = true;  
    if ( fake_pad_video_tee_ ) {
        gst_element_release_request_pad(video_tee_, fake_pad_video_tee_);
        gst_object_unref(fake_pad_video_tee_);
        fake_pad_video_tee_ = NULL;
    }
    if ( fake_pad_audio_tee_ ) {
        gst_element_release_request_pad(audio_tee_, fake_pad_audio_tee_);
        gst_object_unref(fake_pad_audio_tee_);
        fake_pad_audio_tee_ = NULL;
    }

    for(auto handle : joint_handles_)
    {
        GST_DEBUG("----------------------");
        ret = ret && remove_pipejoint_handle(handle);
    }
    joint_handles_.clear();

    IApp::Destroy(promise);
    return ret;
}
/////////////////////////////////////////////////////////////////////////////////////////
void HLStream::On(Promise *promise)
{
    const json &j = promise->meta();
    std::string action = j["action"];
    if (action == "add_performer") {
        add_performer(promise);
    } else if (action == "add_audience") {
        add_audience(promise);
    } else if (action == "remove_audience") {
        remove_audience(promise);
    } else if (action == "startup") {
        Startup(promise);
    } else if (action == "stop") {
        Stop(promise);
    } else {
        GST_ERROR("[hlstream: %s] action: %s is not supported!", 
                  uname().c_str(), action.c_str());
        promise->reject("action: " + action + " is not supported!");
    }
}
/**
 * add_performer
 * {//meta
 *   "action" : "add_performer"
 * }
 * {//data
 *   "name"        : "performer_1",
 *   "url"    : "xxx.mp4"
 *   "video_codec" : "h264"
 *   "audio_codec" : "aac"
 *   //FIXME other properties
 *   //"protocol"    : "filesource",
 * }
 */
void HLStream::add_performer(Promise *promise)
{
    if (performer_ != NULL) {
        GST_ERROR("[hlstream: %s] there's already a performer!", uname().c_str());
        promise->reject("there's already a performer!");
        return;
    }
    //create endpoint
    const json &j = promise->data();
    const std::string &name = j["name"];
    performer_ = new FileSource(this, name);
    //initialize endpoint and add it to the pipeline
    bool rc = performer_->initialize(promise);
    if(rc) {
        // link endpoint to video/audio tee
        if (on_add_endpoint(performer_)) {
            GST_INFO("[hlstream: %s] add performer: %s (type: %s)",
                     uname().c_str(), name.c_str(), 
                     performer_->protocol().c_str());
            promise->resolve();
            return;
        }
    }

    performer_->terminate();
    delete performer_;
    performer_ = NULL;
    GST_ERROR("[hlstream: %s] add performer: %s failed!",
              uname().c_str(), name.c_str());
    promise->reject("add performer " + name + " failed!");
}


std::list<IEndpoint *>::iterator 
HLStream::find_audience(const std::string &name)
{
    return std::find_if(audiences_.begin(), audiences_.end(),
                        [&name](IEndpoint *ep) {
                            return ep->name() == name;
                        });
}

/**
 * add_audience
 * {//meta
 *   "action" : "add_audience"
 * }
 * {//data
 *   "name"               : "audience_1",
 *   "protocol"           : "hlsservice",
 *   "location"           : "segment%d.ts",
 *   "target-duration"    : "15"
 *   "playlist-location"  : "playlist.m3u8"
 *   "playlist-length"    : "5"
 *   //FIXME other properties
 * }
 */
void HLStream::add_audience(Promise *promise)
{
    const json &j = promise->data();
    if (j.find("protocol") == j.end()) {
        GST_ERROR("[hlstream: %s] no protocol in audience.", uname().c_str());
        promise->reject("[hlstream] no protocol in audience.");
        return;
    }
    const std::string &name = j["name"];
    const std::string protocol = j["protocol"];
    
    if (find_audience(name) != audiences_.end()) {
        GST_ERROR("[hlstream: %s] audience: %s has been added.", 
                  uname().c_str(), name.c_str());
        promise->reject("[hlstream] audience: " + name + " has been added.");
        return;
    }

    // create endpoint
    IEndpoint *ep;
    switch ( get_endpoint_type(protocol) ) {
        case EndpointType::HLS_SERVICE:
        {
            ep = new HLSService(this, name);
        }
        break;
        
        default:
        GST_ERROR("[hlstream: %s] protocol: %s not supported.",
                  uname().c_str(), protocol.c_str());
        promise->reject("[hlstream] protocol: " + protocol + " not supported.");
        return;
    }
    ep->protocol() = protocol;
     // add endpoint to pipeline and link with tee
    bool rc = ep->initialize(promise);
    if (rc) {
        audiences_.push_back(ep);
        GST_INFO("[hlstream: %s] add audience: %s (type: %s)",
                 uname().c_str(), name.c_str(), protocol.c_str());
        promise->resolve();
        return;
    }
    ep->terminate();
    delete ep;
    GST_ERROR("[hlstream: %s] add audience: %s failed!",
              uname().c_str(), name.c_str());
    promise->reject("add audience " + name + " failed!");
}

bool HLStream::add_fake_endpoint(bool video)
{
    static bool video_added = false;
    static bool audio_added = false;

    if(!video_added && video) {//video
        GstElement *queue = gst_element_factory_make("queue", "fake_video_queue");
        GstElement *sink = gst_element_factory_make("fakesink", "fake_video_sink");
        g_object_set( sink, "sync", FALSE, NULL );
        gst_bin_add_many( GST_BIN(pipeline()), queue, sink, NULL );
        gst_element_link_many(queue, sink, NULL);

        GstPadTemplate * templ = gst_element_get_pad_template(video_tee_, "src_%u");
        fake_pad_video_tee_ = gst_element_request_pad(video_tee_, templ, NULL, NULL);
        GstPad *sinkpad = gst_element_get_static_pad(queue, "sink");
        g_return_val_if_fail( gst_pad_link(fake_pad_video_tee_, sinkpad) == GST_PAD_LINK_OK , false );
        gst_object_unref(sinkpad);

        video_added = true;

        GST_INFO("[hlstream: %s] fake video endpoint added!", uname().c_str());
    } 
    
    if(!audio_added && !video) {//audio
        GstElement *queue = gst_element_factory_make("queue", "fake_audio_queue");
        GstElement *sink = gst_element_factory_make("fakesink", "fake_audio_sink");
        g_object_set( sink, "sync", FALSE, NULL );
        gst_bin_add_many( GST_BIN(pipeline()), queue, sink, NULL );
        gst_element_link_many(queue, sink, NULL);

        GstPadTemplate * templ = gst_element_get_pad_template(audio_tee_, "src_%u");
        fake_pad_audio_tee_ = gst_element_request_pad(audio_tee_, templ, NULL, NULL);
        GstPad *sinkpad = gst_element_get_static_pad(queue, "sink");
        g_return_val_if_fail( gst_pad_link(fake_pad_audio_tee_, sinkpad) == GST_PAD_LINK_OK , false );
        gst_object_unref(sinkpad);

        audio_added = true;

        GST_INFO("[hlstream: %s] fake audio endpoint added!", uname().c_str());
    }

    return true;
}

bool HLStream::on_add_endpoint(IEndpoint *endpoint)
{
    switch (get_endpoint_type(endpoint->protocol())) {
        case EndpointType::FILE_SOURCE:
        {
            if ( !video_encoding().empty() ) {
                GstElement *parse = gst_bin_get_by_name(GST_BIN(pipeline()), "video_parse");
                g_warn_if_fail(parse);
                g_warn_if_fail(gst_element_link(parse, video_tee_));
                //g_warn_if_fail(add_fake_endpoint(true));
            }
            if ( !audio_encoding().empty() ) {
                GstElement *parse = gst_bin_get_by_name(GST_BIN(pipeline()), "audio_parse");
                g_warn_if_fail(parse);
                g_warn_if_fail(gst_element_link(parse, audio_tee_));
                //g_warn_if_fail(add_fake_endpoint(false));
            }
        }
        break;
        
        default:
        g_warn_if_reached();
        return false;
    }
    return true;
}

/**
 * remove_audience
 * {//meta
 *   "action" : "remove_audience"
 * }
 * {//data
 *   "name"               : "audience_1"
 *   //FIXME other properties
 * }
 */
void HLStream::remove_audience(Promise *promise)
{
    const json &j = promise->data();
    const std::string &name = j["name"];
    auto it = find_audience(name);
    if (it == audiences_.end()) {
        GST_ERROR("[hlstream: %s] audience: %s has not been added.",
                  uname().c_str(), name.c_str());
        promise->reject("[hlstream] audience: " + name + " has not been added.");
        return;
    }
    IEndpoint *ep = *it;
    ep->terminate();
    delete ep;
    audiences_.erase(it);

    GST_INFO("[hlstream: %s] remove audience: %s (type: %s)", 
              uname().c_str(), ep->name().c_str(), ep->protocol().c_str());
    
    promise->resolve();
}

void HLStream::Startup(Promise *promise)
{
    if (!performer_) {
        GST_ERROR("[hlstream: %s] there's no performer now, can't startup!", uname().c_str());
        promise->reject("there's no performer now, can't startup!");
        return;
    }
    gst_element_set_state(pipeline(), GST_STATE_PLAYING);

    GST_INFO("[hlstream: %s] startup", uname().c_str());

    promise->resolve();
}

void HLStream::Stop(Promise *promise)
{
    gst_element_set_state(pipeline(), GST_STATE_NULL);
    if (performer_) {
        GST_DEBUG("[hlstream: %s] remove performer: %s (type: %s)",
                  uname().c_str(), performer_->name().c_str(),
                  performer_->protocol().c_str());

        performer_->terminate();
        delete performer_;
        performer_ = NULL;
    }
    for (auto audience : audiences_) {
        GST_DEBUG("[hlstream: %s] remove audience: %s (type: %s)",
                  uname().c_str(), audience->name().c_str(),
                  audience->protocol().c_str());
        audience->terminate();
        delete audience;
    }
    audiences_.clear();

    promise->resolve();
}

void HLStream::add_pipe_joint(GstElement *upstream_joint)
{
    HLSTREAM_JOINT_LOCK(this);
    gchar *media_type = (gchar *)g_object_get_data(G_OBJECT(upstream_joint), "media-type");
    GstPad *srcpad = NULL;
    if ( g_str_equal(media_type, "video") ) {
        GstPadTemplate *templ = gst_element_get_pad_template(video_tee_, "src_%u");
        srcpad = gst_element_request_pad(video_tee_, templ, NULL, NULL);
    } else 
    if( g_str_equal(media_type, "audio") ) {
        GstPadTemplate *templ = gst_element_get_pad_template(audio_tee_, "src_%u");
        srcpad = gst_element_request_pad(audio_tee_, templ, NULL, NULL);
    }
    g_return_if_fail( srcpad != NULL );
    
    GST_DEBUG("[hlstream: %s] add pipe joint: %s", uname().c_str(), media_type);
    
    //gst_element_set_locked_state(upstream_joint, TRUE);
    g_warn_if_fail( gst_bin_add( GST_BIN(pipeline()), upstream_joint ) );
    
    GstPad *sinkpad = gst_element_get_static_pad(upstream_joint, "sink");
    g_warn_if_fail( gst_pad_link(srcpad, sinkpad) == GST_PAD_LINK_OK );
    gst_object_unref(sinkpad);
    
    //gst_element_set_locked_state(upstream_joint, FALSE);
    gst_element_sync_state_with_parent (upstream_joint);
    
    
    joint_handles_.push_back(new PipeJointHandle(srcpad, upstream_joint, this));
    HLSTREAM_JOINT_UNLOCK(this);
}

void HLStream::remove_pipe_joint(GstElement *upstream_joint)
{
    HLSTREAM_JOINT_LOCK(this);
    PipeJointHandle *handle = NULL;
    
    auto it = std::find_if(joint_handles_.begin(), joint_handles_.end(), 
                           [&upstream_joint](PipeJointHandle *h){
                               return h->upstream_joint == upstream_joint;
                           });
    if(it != joint_handles_.end()) {
        handle = *it;
        joint_handles_.erase(it);
    }
    HLSTREAM_JOINT_UNLOCK(this);
    g_return_if_fail(handle != NULL);

    GST_DEBUG("[hlstream: %s] remove pipe joint", uname().c_str());

    gst_pad_add_probe(handle->tee_srcpad, GST_PAD_PROBE_TYPE_IDLE, 
                      on_tee_srcpad_remove_probe, handle, NULL);
}

GstPadProbeReturn HLStream::on_tee_srcpad_remove_probe(GstPad *srcpad, 
                                                       GstPadProbeInfo *probe_info,
                                                       gpointer joint_handle)
{
    PipeJointHandle *handle = static_cast<PipeJointHandle *>(joint_handle);
    g_return_val_if_fail(handle->tee_srcpad == srcpad, GST_PAD_PROBE_OK);

    HLStream * pipeline = static_cast<HLStream *>(handle->pipeline);
    bool ret = pipeline->remove_pipejoint_handle(handle);
    g_return_val_if_fail(ret, GST_PAD_PROBE_OK);

    GST_DEBUG("[hlstream: %s] on_tee_srcpad_remove_probe", pipeline->uname().c_str());

    return GST_PAD_PROBE_REMOVE;
}

bool HLStream::remove_pipejoint_handle(PipeJointHandle *handle)
{
    GstElement *tee = NULL;
    gchar *media_type = (gchar *)g_object_get_data(G_OBJECT(handle->upstream_joint), "media-type");
    
    if ( g_str_equal(media_type, "video") ) {
        tee = this->video_tee_;
    } else 
    if( g_str_equal(media_type, "audio") ) {
        tee = this->audio_tee_;
    }
    g_return_val_if_fail(tee != NULL , false);
    GST_DEBUG("====audio(%p)=video(%p)=====tee:%p=========%s",this->audio_tee_, this->video_tee_, tee, media_type);
    //gst_element_set_locked_state(handle->upstream_joint, TRUE);
    GstPad *sinkpad = gst_element_get_static_pad(handle->upstream_joint, "sink");
    g_warn_if_fail(gst_pad_unlink(handle->tee_srcpad, sinkpad));
    gst_object_unref(sinkpad);
    gst_element_set_state(handle->upstream_joint, GST_STATE_NULL);
    //gst_element_set_locked_state(handle->upstream_joint, FALSE);
    g_warn_if_fail(gst_bin_remove(GST_BIN(this->pipeline()), handle->upstream_joint));

    gst_element_release_request_pad(tee, handle->tee_srcpad);
    gst_object_unref(handle->tee_srcpad);
    delete handle;

    GST_DEBUG("[hlstream: %s] pipe joint removed (%s)", uname().c_str(), media_type);
    g_free(media_type);

    return true;
}