/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.db.BrowserContract;
import org.mozilla.goanna.db.BrowserDB;
import org.mozilla.goanna.mozglue.GoannaLoader;
import org.mozilla.goanna.util.Clipboard;
import org.mozilla.goanna.util.HardwareUtils;
import org.mozilla.goanna.util.ThreadUtils;

import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

public class GoannaApplication extends Application {

    private boolean mInited;
    private boolean mInBackground;
    private boolean mPausedGoanna;
    private boolean mNeedsRestart;

    private LightweightTheme mLightweightTheme;

    protected void initialize() {
        if (mInited)
            return;

        // workaround for http://code.google.com/p/android/issues/detail?id=20915
        try {
            Class.forName("android.os.AsyncTask");
        } catch (ClassNotFoundException e) {}

        mLightweightTheme = new LightweightTheme(this);

        GoannaConnectivityReceiver.getInstance().init(getApplicationContext());
        GoannaBatteryManager.getInstance().init(getApplicationContext());
        GoannaBatteryManager.getInstance().start();
        GoannaNetworkManager.getInstance().init(getApplicationContext());
        MemoryMonitor.getInstance().init(getApplicationContext());

        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                mNeedsRestart = true;
            }
        };
        registerReceiver(receiver, new IntentFilter(Intent.ACTION_LOCALE_CHANGED));

        mInited = true;
    }

    protected void onActivityPause(GoannaActivityStatus activity) {
        mInBackground = true;

        if ((activity.isFinishing() == false) &&
            (activity.isGoannaActivityOpened() == false)) {
            // Notify Goanna that we are pausing; the cache service will be
            // shutdown, closing the disk cache cleanly. If the android
            // low memory killer subsequently kills us, the disk cache will
            // be left in a consistent state, avoiding costly cleanup and
            // re-creation. 
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createAppBackgroundingEvent());
            mPausedGoanna = true;

            ThreadUtils.postToBackgroundThread(new Runnable() {
                @Override
                public void run() {
                    BrowserDB.expireHistory(getContentResolver(),
                                            BrowserContract.ExpirePriority.NORMAL);
                }
            });
        }
        GoannaConnectivityReceiver.getInstance().stop();
        GoannaNetworkManager.getInstance().stop();
    }

    protected void onActivityResume(GoannaActivityStatus activity) {
        if (mPausedGoanna) {
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createAppForegroundingEvent());
            mPausedGoanna = false;
        }
        GoannaConnectivityReceiver.getInstance().start();
        GoannaNetworkManager.getInstance().start();

        mInBackground = false;
    }

    protected boolean needsRestart() {
        return mNeedsRestart;
    }

    @Override
    public void onCreate() {
        HardwareUtils.init(getApplicationContext());
        Clipboard.init(getApplicationContext());
        GoannaLoader.loadMozGlue(getApplicationContext());
        super.onCreate();
    }

    public boolean isApplicationInBackground() {
        return mInBackground;
    }

    public LightweightTheme getLightweightTheme() {
        return mLightweightTheme;
    }
}
