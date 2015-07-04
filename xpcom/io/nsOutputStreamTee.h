/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSOUTPUTSTREAMTEE_H_
#define NSOUTPUTSTREAMTEE_H_

#include "nsCOMPtr.h"
#include "nsIOutputStream.h"

using namespace mozilla;

class nsOutputStreamTee : public nsIOutputStream
{
public:
    nsOutputStreamTee(nsIOutputStream * stream1, nsIOutputStream * stream2);
    virtual ~nsOutputStreamTee();

    NS_DECL_ISUPPORTS
    NS_DECL_NSIOUTPUTSTREAM

private:
    nsCOMPtr<nsIOutputStream> mStream1;
    nsCOMPtr<nsIOutputStream> mStream2;
    bool                      mClosed;
};


#endif /* NSOUTPUTSTREAMTEE_H_ */
