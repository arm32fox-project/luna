/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.util.GoannaEventResponder;
import org.mozilla.goanna.util.ThreadUtils;

import org.json.JSONObject;

import android.content.Context;

import java.util.concurrent.ConcurrentLinkedQueue;

public class PromptService implements GoannaEventResponder {
    private static final String LOGTAG = "GoannaPromptService";

    private final ConcurrentLinkedQueue<String> mPromptQueue;
    private final Context mContext;

    public PromptService(Context context) {
        GoannaAppShell.getEventDispatcher().registerEventListener("Prompt:Show", this);
        mPromptQueue = new ConcurrentLinkedQueue<String>();
        mContext = context;
    }

    void destroy() {
        GoannaAppShell.getEventDispatcher().unregisterEventListener("Prompt:Show", this);
    }

    public void show(final String aTitle, final String aText, final Prompt.PromptListItem[] aMenuList,
                     final boolean aMultipleSelection, final Prompt.PromptCallback callback) {
        // The dialog must be created on the UI thread.
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                Prompt p;
                if (callback != null) {
                    p = new Prompt(mContext, callback);
                } else {
                    p = new Prompt(mContext, mPromptQueue);
                }
                p.show(aTitle, aText, aMenuList, aMultipleSelection);
            }
        });
    }

    // GoannaEventListener implementation
    @Override
    public void handleMessage(String event, final JSONObject message) {
        // The dialog must be created on the UI thread.
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                boolean isAsync = message.optBoolean("async");
                Prompt p;
                if (isAsync) {
                    p = new Prompt(mContext, new Prompt.PromptCallback() {
                        public void onPromptFinished(String jsonResult) {
                            GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent("Prompt:Reply", jsonResult));
                        }
                    });
                } else {
                    p = new Prompt(mContext, mPromptQueue);
                }
                p.show(message);
            }
        });
    }

    // GoannaEventResponder implementation
    @Override
    public String getResponse(final JSONObject origMessage) {
        if (origMessage.optBoolean("async")) {
            return "";
        }

        // we only handle one kind of message in handleMessage, and this is the
        // response we provide for that message
        String result;
        while (null == (result = mPromptQueue.poll())) {
            GoannaAppShell.processNextNativeEvent(true);
        }
        return result;
    }
}
