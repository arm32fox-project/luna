/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#include "jswin.h"

#include "js/Utility.h"

#include "threading/Mutex.h"
#include "threading/windows/MutexPlatformData.h"

js::detail::MutexImpl::MutexImpl()
{
  AutoEnterOOMUnsafeRegion oom;
  platformData_ = js_new<PlatformData>();
  if (!platformData_)
    oom.crash("js::Mutex::Mutex");

  InitializeSRWLock(&platformData()->lock);
}

js::detail::MutexImpl::~MutexImpl()
{
  if (!platformData_)
    return;

  js_delete(platformData());
}

void
js::detail::MutexImpl::lock()
{
  AcquireSRWLockExclusive(&platformData()->lock);
}

void
js::detail::MutexImpl::unlock()
{
  ReleaseSRWLockExclusive(&platformData()->lock);
}
