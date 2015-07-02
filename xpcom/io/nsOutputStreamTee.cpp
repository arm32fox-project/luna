/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsOutputStreamTee.h"
#include "prlog.h"

#ifdef PR_LOGGING
static PRLogModuleInfo*
GetTeeLog()
{
    static PRLogModuleInfo *sLog;
    if (!sLog)
        sLog = PR_NewLogModule("nsStreamTees");
    return sLog;
}
#define LOG(args) PR_LOG(GetTeeLog(), PR_LOG_DEBUG, args)
#else
#define LOG(args)
#endif

NS_IMPL_THREADSAFE_ISUPPORTS1(nsOutputStreamTee,
                              nsIOutputStream)

nsOutputStreamTee::nsOutputStreamTee(nsIOutputStream * stream1, nsIOutputStream * stream2)
    : mStream1(stream1)
    , mStream2(stream2)
    , mClosed(false)
{
    LOG(("OS Tee[%p] s1=%p s2=%p", this, stream1, stream2));
}


nsOutputStreamTee::~nsOutputStreamTee()
{
    Close();
}


NS_IMETHODIMP
nsOutputStreamTee::Close()
{
    LOG(("nsOutputStreamTee::Closing [%p]", this));
    if (!mClosed) {
        mClosed = true;
        mStream1->Close();
        mStream2->Close();
    }
    return NS_OK;
}

NS_IMETHODIMP
nsOutputStreamTee::Flush()
{
    LOG(("nsOutputStreamTee::Flush[%p]  mClosed=%d", this, mClosed));
    if (mClosed)  return NS_BASE_STREAM_CLOSED;
    return NS_OK;
}


NS_IMETHODIMP
nsOutputStreamTee::Write(const char *buf, PRUint32 count, PRUint32 *bytesWritten)
{
    if (mClosed)  return NS_BASE_STREAM_CLOSED;

    LOG(("nsOutputStreamTee::Write[%p] will write %d bytes", this, count));
    nsresult rv = NS_OK;
    PRUint32 writtenBytes;

    rv = mStream1->Write(buf, count, &writtenBytes);
    if (NS_SUCCEEDED(rv)) {
        LOG((" wrote %d bytes to s1", writtenBytes));
        if (writtenBytes != count)
            return NS_ERROR_UNEXPECTED;

        rv = mStream2->Write(buf, count, &writtenBytes);
        if (NS_SUCCEEDED(rv)) {
            LOG((" wrote %d bytes to s2", writtenBytes));
            if (writtenBytes != count)
                return NS_ERROR_UNEXPECTED;
        }
    }
    *bytesWritten = writtenBytes;
    return rv;
}


NS_IMETHODIMP
nsOutputStreamTee::WriteFrom(nsIInputStream *inStream, PRUint32 count, PRUint32 *bytesWritten)
{
    NS_NOTREACHED("WriteFrom");
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
nsOutputStreamTee::WriteSegments( nsReadSegmentFun reader,
                                        void *           closure,
                                        PRUint32         count,
                                        PRUint32 *       bytesWritten)
{
    NS_NOTREACHED("WriteSegments");
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
nsOutputStreamTee::IsNonBlocking(bool * nonBlocking)
{
    bool tmp;
    nsresult rv;
    if (NS_FAILED(rv = mStream1->IsNonBlocking(&tmp)))
        return rv;

    *nonBlocking = tmp;

    if (NS_FAILED(rv = mStream2->IsNonBlocking(&tmp)))
        return rv;

    // This tee blocks if any of the streams are blocking.
    *nonBlocking = tmp && *nonBlocking;
    return NS_OK;
}
