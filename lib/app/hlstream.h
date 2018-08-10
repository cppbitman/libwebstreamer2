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

#ifndef _LIBWEBSTREAMER_APP_HLS_STREAM_H_
#define _LIBWEBSTREAMER_APP_HLS_STREAM_H_

#include <framework/app.h>
#include <mutex>

struct PipeJointHandle
{
    IApp *pipeline;
    GstPad *tee_srcpad;
    GstElement *upstream_joint;

    PipeJointHandle(GstPad *srcpad, GstElement *joint, IApp *pipe)
        : upstream_joint(joint)
        , tee_srcpad(srcpad)
        , pipeline(pipe)
    {
    }
};

class HLStream : public IApp
{
public:
    APP(HLStream);

    HLStream(const std::string &name, WebStreamer *ws);
    ~HLStream();
    void add_pipe_joint(GstElement *upstream_joint);
    void remove_pipe_joint(GstElement *upstream_joint);

    virtual void On(Promise *promise);
    virtual bool Initialize(Promise *promise);
    virtual bool Destroy(Promise *promise);

protected:
    void add_performer(Promise *promise);
    void add_audience(Promise *promise);
    void remove_audience(Promise *promise);
    void Startup(Promise *promise);
    void Stop(Promise *promise);

private:
    bool on_add_endpoint(IEndpoint *endpoint);
    bool add_fake_endpoint(bool video);
    std::list<IEndpoint *>::iterator
        find_audience(const std::string &name);
    
    static GstPadProbeReturn
    on_tee_srcpad_remove_probe(GstPad *srcpad, 
                               GstPadProbeInfo *probe_info,
                               gpointer joint_handle);
    
    bool remove_pipejoint_handle(PipeJointHandle *handle);

private:
    GstElement *video_tee_;
    GstElement *audio_tee_;

    GstPad *fake_pad_video_tee_;
    GstPad *fake_pad_audio_tee_;
    
    IEndpoint *performer_;
    std::list<IEndpoint *> audiences_;
    std::list<PipeJointHandle *> joint_handles_;
    std::mutex joint_mutex_;
};

#endif