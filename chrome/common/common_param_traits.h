// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is used to define IPC::ParamTraits<> specializations for a number
// of types so that they can be serialized over IPC.  IPC::ParamTraits<>
// specializations for basic types (like int and std::string) and types in the
// 'base' project can be found in ipc/ipc_message_utils.h.  This file contains
// specializations for types that are shared by more than one child process.

#ifndef CHROME_COMMON_COMMON_PARAM_TRAITS_H_
#define CHROME_COMMON_COMMON_PARAM_TRAITS_H_
#pragma once

#if defined(OS_WIN)
#include <windows.h>
#endif

#include "app/surface/transport_dib.h"
#include "base/file_util.h"
#include "base/ref_counted.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/page_zoom.h"
#include "gfx/native_widget_types.h"
#include "ipc/ipc_message_utils.h"
#include "net/url_request/url_request_status.h"
#include "printing/native_metafile.h"
#include "webkit/glue/webcursor.h"
#include "webkit/glue/window_open_disposition.h"

// Forward declarations.
struct Geoposition;
class GURL;
class SkBitmap;
class DictionaryValue;
class ListValue;
struct ThumbnailScore;
class URLRequestStatus;
class WebCursor;

namespace gfx {
class Point;
class Rect;
class Size;
}  // namespace gfx

namespace net {
class UploadData;
}

namespace printing {
struct PageRange;
}  // namespace printing

namespace webkit_blob {
class BlobData;
}

namespace webkit_glue {
struct PasswordForm;
struct WebApplicationInfo;
}  // namespace webkit_glue

namespace IPC {

template <>
struct ParamTraits<SkBitmap> {
  typedef SkBitmap param_type;
  static void Write(Message* m, const param_type& p);

  // Note: This function expects parameter |r| to be of type &SkBitmap since
  // r->SetConfig() and r->SetPixels() are called.
  static bool Read(const Message* m, void** iter, param_type* r);

  static void Log(const param_type& p, std::string* l);
};


template <>
struct ParamTraits<GURL> {
  typedef GURL param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};


template <>
struct ParamTraits<gfx::Point> {
  typedef gfx::Point param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<gfx::Rect> {
  typedef gfx::Rect param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<gfx::Size> {
  typedef gfx::Size param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<ContentSetting> {
  typedef ContentSetting param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<ContentSettingsType> {
  typedef ContentSettingsType param_type;
  static void Write(Message* m, const param_type& p) {
    WriteParam(m, static_cast<int>(p));
  }
  static bool Read(const Message* m, void** iter, param_type* r) {
    int value;
    if (!ReadParam(m, iter, &value))
      return false;
    if (value < 0 || value >= static_cast<int>(CONTENT_SETTINGS_NUM_TYPES))
      return false;
    *r = static_cast<param_type>(value);
    return true;
  }
  static void Log(const param_type& p, std::string* l) {
    LogParam(static_cast<int>(p), l);
  }
};

template <>
struct ParamTraits<ContentSettings> {
  typedef ContentSettings param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<gfx::NativeWindow> {
  typedef gfx::NativeWindow param_type;
  static void Write(Message* m, const param_type& p) {
#if defined(OS_WIN)
    // HWNDs are always 32 bits on Windows, even on 64 bit systems.
    m->WriteUInt32(reinterpret_cast<uint32>(p));
#else
    m->WriteData(reinterpret_cast<const char*>(&p), sizeof(p));
#endif
  }
  static bool Read(const Message* m, void** iter, param_type* r) {
#if defined(OS_WIN)
    return m->ReadUInt32(iter, reinterpret_cast<uint32*>(r));
#else
    const char *data;
    int data_size = 0;
    bool result = m->ReadData(iter, &data, &data_size);
    if (result && data_size == sizeof(gfx::NativeWindow)) {
      memcpy(r, data, sizeof(gfx::NativeWindow));
    } else {
      result = false;
      NOTREACHED();
    }
    return result;
#endif
  }
  static void Log(const param_type& p, std::string* l) {
    l->append("<gfx::NativeWindow>");
  }
};

template <>
struct ParamTraits<PageZoom::Function> {
  typedef PageZoom::Function param_type;
  static void Write(Message* m, const param_type& p) {
    WriteParam(m, static_cast<int>(p));
  }
  static bool Read(const Message* m, void** iter, param_type* r) {
    int value;
    if (!ReadParam(m, iter, &value))
      return false;
    *r = static_cast<param_type>(value);
    return true;
  }
  static void Log(const param_type& p, std::string* l) {
    LogParam(static_cast<int>(p), l);
  }
};


template <>
struct ParamTraits<WindowOpenDisposition> {
  typedef WindowOpenDisposition param_type;
  static void Write(Message* m, const param_type& p) {
    WriteParam(m, static_cast<int>(p));
  }
  static bool Read(const Message* m, void** iter, param_type* r) {
    int value;
    if (!ReadParam(m, iter, &value))
      return false;
    *r = static_cast<param_type>(value);
    return true;
  }
  static void Log(const param_type& p, std::string* l) {
    LogParam(static_cast<int>(p), l);
  }
};


template <>
struct ParamTraits<WebCursor> {
  typedef WebCursor param_type;
  static void Write(Message* m, const param_type& p) {
    p.Serialize(m);
  }
  static bool Read(const Message* m, void** iter, param_type* r)  {
    return r->Deserialize(m, iter);
  }
  static void Log(const param_type& p, std::string* l) {
    l->append("<WebCursor>");
  }
};


template <>
struct ParamTraits<webkit_glue::WebApplicationInfo> {
  typedef webkit_glue::WebApplicationInfo param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};


#if defined(OS_WIN)
template <>
struct ParamTraits<TransportDIB::Handle> {
  typedef TransportDIB::Handle param_type;
  static void Write(Message* m, const param_type& p) {
    WriteParam(m, p.section());
    WriteParam(m, p.owner_id());
    WriteParam(m, p.should_dup_on_map());
  }
  static bool Read(const Message* m, void** iter, param_type* r) {
    HANDLE section;
    base::ProcessId owner_id;
    bool should_dup_on_map;
    bool success = ReadParam(m, iter, &section) &&
                   ReadParam(m, iter, &owner_id) &&
                   ReadParam(m, iter, &should_dup_on_map);
    if (success) {
      *r = TransportDIB::Handle(section, owner_id, should_dup_on_map);
    }
    return success;
  }
  static void Log(const param_type& p, std::string* l) {
    l->append("TransportDIB::Handle(");
    LogParam(p.section(), l);
    l->append(", ");
    LogParam(p.owner_id(), l);
    l->append(", ");
    LogParam(p.should_dup_on_map(), l);
    l->append(")");
  }
};
#endif

// Traits for URLRequestStatus
template <>
struct ParamTraits<URLRequestStatus> {
  typedef URLRequestStatus param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

// Traits for net::UploadData.
template <>
struct ParamTraits<scoped_refptr<net::UploadData> > {
  typedef scoped_refptr<net::UploadData> param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

// Traits for webkit_blob::BlobData.
template <>
struct ParamTraits<scoped_refptr<webkit_blob::BlobData> > {
  typedef scoped_refptr<webkit_blob::BlobData> param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template<>
struct ParamTraits<ThumbnailScore> {
  typedef ThumbnailScore param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<Geoposition> {
  typedef Geoposition param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<webkit_glue::PasswordForm> {
  typedef webkit_glue::PasswordForm param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* p);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<printing::PageRange> {
  typedef printing::PageRange param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<printing::NativeMetafile> {
  typedef printing::NativeMetafile param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<base::PlatformFileInfo> {
  typedef base::PlatformFileInfo param_type;
  static void Write(Message* m, const param_type& p);
  static bool Read(const Message* m, void** iter, param_type* r);
  static void Log(const param_type& p, std::string* l);
};

}  // namespace IPC

#endif  // CHROME_COMMON_COMMON_PARAM_TRAITS_H_
