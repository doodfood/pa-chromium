// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_MEDIA_STREAM_VIDEO_TRACK_RESOURCE_H_
#define PPAPI_PROXY_MEDIA_STREAM_VIDEO_TRACK_RESOURCE_H_

#include <map>

#include "base/memory/ref_counted.h"
#include "ppapi/proxy/media_stream_track_resource_base.h"
#include "ppapi/proxy/ppapi_proxy_export.h"
#include "ppapi/thunk/ppb_media_stream_video_track_api.h"

namespace ppapi {
namespace proxy {

class VideoFrameResource;

class PPAPI_PROXY_EXPORT MediaStreamVideoTrackResource
    : public MediaStreamTrackResourceBase,
      public thunk::PPB_MediaStreamVideoTrack_API {
 public:
  MediaStreamVideoTrackResource(Connection connection,
                                PP_Instance instance,
                                int pending_renderer_id,
                                const std::string& id);

  virtual ~MediaStreamVideoTrackResource();

  // Resource overrides:
  virtual thunk::PPB_MediaStreamVideoTrack_API*
  AsPPB_MediaStreamVideoTrack_API() OVERRIDE;

  // PPB_MediaStreamVideoTrack_API overrides:
  virtual PP_Var GetId() OVERRIDE;
  virtual PP_Bool HasEnded() OVERRIDE;
  virtual int32_t Configure(uint32_t max_buffered_frames) OVERRIDE;
  virtual int32_t GetFrame(
      PP_Resource* frame,
      scoped_refptr<TrackedCallback> callback) OVERRIDE;
  virtual int32_t RecycleFrame(PP_Resource frame) OVERRIDE;
  virtual void Close() OVERRIDE;

  // MediaStreamFrameBuffer::Delegate overrides:
  virtual void OnNewFrameEnqueued() OVERRIDE;

 private:
  PP_Resource GetVideoFrame();

  void ReleaseFrames();

  // Allocated frame resources by |GetFrame()|.
  typedef std::map<PP_Resource, scoped_refptr<VideoFrameResource> > FrameMap;
  FrameMap frames_;

  PP_Resource* get_frame_output_;

  scoped_refptr<TrackedCallback> get_frame_callback_;

  DISALLOW_COPY_AND_ASSIGN(MediaStreamVideoTrackResource);
};

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PROXY_MEDIA_STREAM_VIDEO_TRACK_RESOURCE_H_
