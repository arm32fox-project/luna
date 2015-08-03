/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.gfx.GoannaLayerClient;
import org.mozilla.goanna.gfx.LayerView;
import org.mozilla.goanna.mozglue.GoannaLoader;
import org.mozilla.goanna.sqlite.SQLiteBridge;
import org.mozilla.goanna.util.GoannaEventListener;

import android.app.Activity;
import android.database.Cursor;
import android.view.View;

import java.nio.IntBuffer;

public class RobocopAPI {
    private final GoannaApp mGoannaApp;

    public RobocopAPI(Activity activity) {
        mGoannaApp = (GoannaApp)activity;
    }

    public void registerEventListener(String event, GoannaEventListener listener) {
        GoannaAppShell.registerEventListener(event, listener);
    }

    public void unregisterEventListener(String event, GoannaEventListener listener) {
        GoannaAppShell.unregisterEventListener(event, listener);
    }

    public void broadcastEvent(String subject, String data) {
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent(subject, data));
    }

    public void setDrawListener(GoannaLayerClient.DrawListener listener) {
        GoannaAppShell.getLayerView().getLayerClient().setDrawListener(listener);
    }

    public Cursor querySql(String dbPath, String query) {
        GoannaLoader.loadSQLiteLibs(mGoannaApp, mGoannaApp.getApplication().getPackageResourcePath());
        return new SQLiteBridge(dbPath).rawQuery(query, null);
    }

    public IntBuffer getViewPixels(View view) {
        return ((LayerView)view).getPixels();
    }
}
