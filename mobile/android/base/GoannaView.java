/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.db.BrowserDB;
import org.mozilla.goanna.gfx.LayerView;
import org.mozilla.goanna.util.GoannaEventListener;
import org.mozilla.goanna.util.ThreadUtils;

import org.json.JSONObject;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.net.Uri;
import android.os.Bundle;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.os.Handler;

public class GoannaView extends LayerView
    implements GoannaEventListener, ContextGetter {
    static GoannaThread sGoannaThread;

    public GoannaView(Context context, AttributeSet attrs) {
        super(context, attrs);

        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.GoannaView);
        String url = a.getString(R.styleable.GoannaView_url);
        a.recycle();

        Intent intent;
        if (url == null) {
            intent = new Intent(Intent.ACTION_MAIN);
        } else {
            intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createURILoadEvent(url));
        }
        GoannaAppShell.setContextGetter(this);
        if (context instanceof Activity) {
            Tabs tabs = Tabs.getInstance();
            tabs.attachToActivity((Activity) context);
        }
        GoannaProfile profile = GoannaProfile.get(context);
        BrowserDB.initialize(profile.getName());
        GoannaAppShell.registerEventListener("Goanna:Ready", this);

        sGoannaThread = new GoannaThread(intent, url);
        ThreadUtils.setGoannaThread(sGoannaThread);
        ThreadUtils.setUiThread(Thread.currentThread(), new Handler());
        initializeView(GoannaAppShell.getEventDispatcher());
        if (GoannaThread.checkAndSetLaunchState(GoannaThread.LaunchState.Launching, GoannaThread.LaunchState.Launched)) {
            GoannaAppShell.setLayerView(this);
            sGoannaThread.start();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);

        if (hasFocus) {
            setBackgroundDrawable(null);
        }
    }

    public void loadUrl(String uri) {
        Tabs.getInstance().loadUrl(uri);
    }

    public void handleMessage(String event, JSONObject message) {
        if (event.equals("Goanna:Ready")) {
            GoannaThread.setLaunchState(GoannaThread.LaunchState.GoannaRunning);
            Tab selectedTab = Tabs.getInstance().getSelectedTab();
            if (selectedTab != null)
                Tabs.getInstance().notifyListeners(selectedTab, Tabs.TabEvents.SELECTED);
            goannaConnected();
            GoannaAppShell.setLayerClient(getLayerClient());
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent("Viewport:Flush", null));
            show();
            requestRender();
        }
    }

    public static void setGoannaInterface(GoannaAppShell.GoannaInterface aGoannaInterface) {
        GoannaAppShell.setGoannaInterface(aGoannaInterface);
    }
}
