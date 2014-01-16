// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/media_stream_video_track_resource.h"

#include "base/logging.h"
#include "ppapi/proxy/video_frame_resource.h"
#include "ppapi/shared_impl/media_stream_frame.h"
#include "ppapi/shared_impl/var.h"

namespace ppapi {
namespace proxy {

MediaStreamVideoTrackResource::MediaStreamVideoTrackResource(
    Connection connection,
    PP_Instance instance,
    int pending_renderer_id,
    const std::string& id)
    : MediaStreamTrackResourceBase(
        connection, instance, pending_renderer_id, id),
      get_frame_output_(NULL) {
}

MediaStreamVideoTrackResource::~MediaStreamVideoTrackResource() {
  Close();
}

thunk::PPB_MediaStreamVideoTrack_API*
MediaStreamVideoTrackResource::AsPPB_MediaStreamVideoTrack_API() {
  return this;
}

PP_Var MediaStreamVideoTrackResource::GetId() {
  return StringVar::StringToPPVar(id());
}

PP_Bool MediaStreamVideoTrackResource::HasEnded() {
  return PP_FromBool(has_ended());
}

int32_t MediaStreamVideoTrackResource::Configure(uint32_t max_buffered_frames) {
  // TODO(penghuang): redesign and implement Configure() to support format,
  // size, etc.
  return PP_ERROR_NOTSUPPORTED;
}

int32_t MediaStreamVideoTrackResource::GetFrame(
    PP_Resource* frame,
    scoped_refptr<TrackedCallback> callback) {
  if (has_ended())
    return PP_ERROR_FAILED;

  if (TrackedCallback::IsPending(get_frame_callback_))
    return PP_ERROR_INPROGRESS;

  *frame = GetVideoFrame();
  if (*frame)
    return PP_OK;

  get_frame_output_ = frame;
  get_frame_callback_ = callback;
  return PP_OK_COMPLETIONPENDING;
}

int32_t MediaStreamVideoTrackResource::RecycleFrame(PP_Resource frame) {
  FrameMap::iterator it = frames_.find(frame);
  if (it == frames_.end())
    return PP_ERROR_BADRESOURCE;

  scoped_refptr<VideoFrameResource> frame_resource = it->second;
  frames_.erase(it);

  if (has_ended())
    return PP_OK;

  DCHECK_GE(frame_resource->GetFrameBufferIndex(), 0);

  SendEnqueueFrameMessageToHost(frame_resource->GetFrameBufferIndex());
  frame_resource->Invalidate();
  return PP_OK;
}

void MediaStreamVideoTrackResource::Close() {
  if (has_ended())
    return;

  if (TrackedCallback::IsPending(get_frame_callback_)) {
    *get_frame_output_ = 0;
    get_frame_callback_->PostAbort();
    get_frame_callback_ = NULL;
    get_frame_output_ = 0;
  }

  ReleaseFrames();
  MediaStreamTrackResourceBase::CloseInternal();
}

void MediaStreamVideoTrackResource::OnNewFrameEnqueued() {
  if (TrackedCallback::IsPending(get_frame_callback_)) {
    *get_frame_output_ = GetVideoFrame();
    get_frame_output_ = NULL;
    scoped_refptr<TrackedCallback> callback;
    callback.swap(get_frame_callback_);
    callback->Run(PP_OK);
  }
}

PP_Resource MediaStreamVideoTrackResource::GetVideoFrame() {
  int32_t index = frame_buffer()->DequeueFrame();
  if (index < 0)
    return 0;
  MediaStreamFrame* frame = frame_buffer()->GetFramePointer(index);
  scoped_refptr<VideoFrameResource> resource =
      new VideoFrameResource(pp_instance(), index, frame);
  // Add |pp_resource()| and |resource| into |frames_|.
  // |frames_| uses scoped_ptr<> to hold a ref of |resource|. It keeps the
  // resource alive.
  frames_.insert(FrameMap::value_type(resource->pp_resource(), resource));
  return resource->GetReference();
}

void MediaStreamVideoTrackResource::ReleaseFrames() {
  FrameMap::iterator it = frames_.begin();
  while (it != frames_.end()) {
    // Just invalidate and release VideoFrameResorce, but keep PP_Resource.
    // So plugin can still use |RecycleFrame()|.
    it->second->Invalidate();
    it->second = NULL;
  }
}

}  // namespace proxy
}  // namespace ppapi
