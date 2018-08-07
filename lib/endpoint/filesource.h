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

#ifndef _LIBWEBSTREAMER_ENDPOINT_FILE_SOURCE_H_
#define _LIBWEBSTREAMER_ENDPOINT_FILE_SOURCE_H_


#include <framework/app.h>

class FileSource : public IEndpoint
{
public:
    FileSource(IApp *app, const std::string &name);
    ~FileSource();

    virtual bool initialize(Promise *promise);
    virtual void terminate();

private:
    bool add_to_pipeline();
    static void on_demux_pad_added(GstElement* demux,
                                   GstPad* src_pad,
                                   gpointer filesource);

private:
    GstElement* filesrc_;//filesrc
    GstElement* demux_;//qtdemux
    GstElement *video_queue_;//queue
    GstElement *audio_queue_;//queue
    GstElement *video_parse_;//h264parse
    GstElement *audio_parse_;//aacparse
    
    bool add_to_pipeline_;
};

#endif