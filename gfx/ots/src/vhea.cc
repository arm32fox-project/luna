// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vhea.h"

#include "head.h"
#include "maxp.h"

// vhea - Vertical Header Table
// http://www.microsoft.com/opentype/otspec/vhea.htm

namespace ots {

bool ots_vhea_parse(OpenTypeFile *file, const uint8_t *data, size_t length) {
  Buffer table(data, length);
  OpenTypeVHEA *vhea = new OpenTypeVHEA;
  file->vhea = vhea;

  if (!table.ReadU32(&vhea->header.version)) {
    return OTS_FAILURE();
  }
  if (vhea->header.version != 0x00010000 &&
      vhea->header.version != 0x00011000) {
    return OTS_FAILURE();
  }

  if (!ParseMetricsHeader(file, &table, &vhea->header)) {
    return OTS_FAILURE();
  }

  return true;
}

bool ots_vhea_should_serialise(OpenTypeFile *file) {
  // vhea should'nt serialise when vmtx doesn't exist.
  return file->vhea != NULL && file->vmtx != NULL;
}

bool ots_vhea_serialise(OTSStream *out, OpenTypeFile *file) {
  if (!SerialiseMetricsHeader(out, &file->vhea->header)) {
    return OTS_FAILURE();
  }
  return true;
}

void ots_vhea_free(OpenTypeFile *file) {
  delete file->vhea;
}

}  // namespace ots

