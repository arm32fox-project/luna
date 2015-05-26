/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.gfx.LayerView;
import org.mozilla.goanna.menu.GoannaMenu;
import org.mozilla.goanna.menu.MenuItemActionBar;
import org.mozilla.goanna.menu.MenuItemDefault;
import org.mozilla.goanna.widget.AboutHomeView;
import org.mozilla.goanna.widget.AddonsSection;
import org.mozilla.goanna.widget.FaviconView;
import org.mozilla.goanna.widget.IconTabWidget;
import org.mozilla.goanna.widget.LastTabsSection;
import org.mozilla.goanna.widget.LinkTextView;
import org.mozilla.goanna.widget.PromoBox;
import org.mozilla.goanna.widget.RemoteTabsSection;
import org.mozilla.goanna.widget.TabRow;
import org.mozilla.goanna.widget.ThumbnailView;
import org.mozilla.goanna.widget.TopSitesView;

import android.content.Context;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;

import java.lang.reflect.Constructor;
import java.util.HashMap;
import java.util.Map;

public final class GoannaViewsFactory implements LayoutInflater.Factory {
    private static final String LOGTAG = "GoannaViewsFactory";

    private static final String GOANNA_VIEW_IDENTIFIER = "org.mozilla.goanna.";
    private static final int GOANNA_VIEW_IDENTIFIER_LENGTH = GOANNA_VIEW_IDENTIFIER.length();

    private static final String GOANNA_IDENTIFIER = "Goanna.";
    private static final int GOANNA_IDENTIFIER_LENGTH = GOANNA_IDENTIFIER.length();

    private final Map<String, Constructor<? extends View>> mFactoryMap;

    private GoannaViewsFactory() {
        // initialize the hashmap to a capacity that is a prime number greater than
        // (size * 4/3). The size is the number of items we expect to put in it, and
        // 4/3 is the inverse of the default load factor.
        mFactoryMap = new HashMap<String, Constructor<? extends View>>(53);
        Class<Context> arg1Class = Context.class;
        Class<AttributeSet> arg2Class = AttributeSet.class;
        try {
            mFactoryMap.put("AboutHomeView", AboutHomeView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("AddonsSection", AddonsSection.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("LastTabsSection", LastTabsSection.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("PromoBox", PromoBox.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("RemoteTabsSection", RemoteTabsSection.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TopSitesView", TopSitesView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("AwesomeBarTabs", AwesomeBarTabs.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("AwesomeBarTabs$BackgroundLayout", AwesomeBarTabs.BackgroundLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("BackButton", BackButton.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("BrowserToolbarBackground", BrowserToolbarBackground.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("CheckableLinearLayout", CheckableLinearLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("FormAssistPopup", FormAssistPopup.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("ForwardButton", ForwardButton.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("GoannaApp$MainLayout", GoannaApp.MainLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("LinkTextView", LinkTextView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("menu.MenuItemActionBar", MenuItemActionBar.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("menu.MenuItemDefault", MenuItemDefault.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("menu.GoannaMenu$DefaultActionItemBar", GoannaMenu.DefaultActionItemBar.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("FindInPageBar", FindInPageBar.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("IconTabWidget", IconTabWidget.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("RemoteTabs", RemoteTabs.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("ShapedButton", ShapedButton.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TabRow", TabRow.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TabsPanel", TabsPanel.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TabsPanel$TabsListContainer", TabsPanel.TabsListContainer.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TabsPanel$TabsPanelToolbar", TabsPanel.TabsPanelToolbar.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TabsTray", TabsTray.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("ThumbnailView", ThumbnailView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TextSelectionHandle", TextSelectionHandle.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("gfx.LayerView", LayerView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("AllCapsTextView", AllCapsTextView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("Button", GoannaButton.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("EditText", GoannaEditText.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("FrameLayout", GoannaFrameLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("ImageButton", GoannaImageButton.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("ImageView", GoannaImageView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("LinearLayout", GoannaLinearLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("RelativeLayout", GoannaRelativeLayout.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TextSwitcher", GoannaTextSwitcher.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("TextView", GoannaTextView.class.getConstructor(arg1Class, arg2Class));
            mFactoryMap.put("FaviconView", FaviconView.class.getConstructor(arg1Class, arg2Class));
        } catch (NoSuchMethodException nsme) {
            Log.e(LOGTAG, "Unable to initialize views factory", nsme);
        }
    }

    // Making this a singleton class.
    private static final GoannaViewsFactory INSTANCE = new GoannaViewsFactory();

    public static GoannaViewsFactory getInstance() {
        return INSTANCE;
    }

    @Override
    public View onCreateView(String name, Context context, AttributeSet attrs) {
        if (!TextUtils.isEmpty(name)) {
            String viewName = null;

            if (name.startsWith(GOANNA_VIEW_IDENTIFIER))
                viewName = name.substring(GOANNA_VIEW_IDENTIFIER_LENGTH);
            else if (name.startsWith(GOANNA_IDENTIFIER))
                viewName = name.substring(GOANNA_IDENTIFIER_LENGTH);
            else
                return null;

            Constructor<? extends View> constructor = mFactoryMap.get(viewName);
            if (constructor != null) {
                try {
                    return constructor.newInstance(context, attrs);
                } catch (Exception e) {
                    Log.e(LOGTAG, "Unable to instantiate view " + name, e);
                    return null;
                }
            }

            Log.d(LOGTAG, "Warning: unknown custom view: " + name);
        }

        return null;
    }
}
