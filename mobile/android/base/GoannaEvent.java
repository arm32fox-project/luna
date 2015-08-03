/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.gfx.DisplayPortMetrics;
import org.mozilla.goanna.gfx.ImmutableViewportMetrics;

import android.content.res.Resources;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.Rect;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorManager;
import android.location.Address;
import android.location.Location;
import android.os.Build;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.nio.ByteBuffer;

/* We're not allowed to hold on to most events given to us
 * so we save the parts of the events we want to use in GoannaEvent.
 * Fields have different meanings depending on the event type.
 */

/* This class is referenced by Robocop via reflection; use care when
 * modifying the signature.
 */
public class GoannaEvent {
    private static final String LOGTAG = "GoannaEvent";

    // Make sure to keep these values in sync with the enum in
    // AndroidGoannaEvent in widget/android/AndroidJavaWrapper.h
    private enum NativeGoannaEvent {
        NATIVE_POKE(0),
        KEY_EVENT(1),
        MOTION_EVENT(2),
        SENSOR_EVENT(3),
        LOCATION_EVENT(5),
        IME_EVENT(6),
        DRAW(7),
        SIZE_CHANGED(8),
        APP_BACKGROUNDING(9),
        APP_FOREGROUNDING(10),
        LOAD_URI(12),
        NOOP(15),
        BROADCAST(19),
        VIEWPORT(20),
        VISITED(21),
        NETWORK_CHANGED(22),
        THUMBNAIL(25),
        SCREENORIENTATION_CHANGED(27),
        COMPOSITOR_CREATE(28),
        COMPOSITOR_PAUSE(29),
        COMPOSITOR_RESUME(30),
        NATIVE_GESTURE_EVENT(31),
        IME_KEY_EVENT(32),
        CALL_OBSERVER(33),
        REMOVE_OBSERVER(34),
        LOW_MEMORY(35),
        NETWORK_LINK_CHANGE(36);

        public final int value;

        private NativeGoannaEvent(int value) {
            this.value = value;
         }
    }

    /**
     * The DomKeyLocation enum encapsulates the DOM KeyboardEvent's constants.
     * @see https://developer.mozilla.org/en-US/docs/DOM/KeyboardEvent#Key_location_constants
     */
    public enum DomKeyLocation {
        DOM_KEY_LOCATION_STANDARD(0),
        DOM_KEY_LOCATION_LEFT(1),
        DOM_KEY_LOCATION_RIGHT(2),
        DOM_KEY_LOCATION_NUMPAD(3),
        DOM_KEY_LOCATION_MOBILE(4),
        DOM_KEY_LOCATION_JOYSTICK(5);

        public final int value;

        private DomKeyLocation(int value) {
            this.value = value;
        }
    }

    // Encapsulation of common IME actions.
    public enum ImeAction {
        IME_SYNCHRONIZE(0),
        IME_REPLACE_TEXT(1),
        IME_SET_SELECTION(2),
        IME_ADD_COMPOSITION_RANGE(3),
        IME_UPDATE_COMPOSITION(4),
        IME_REMOVE_COMPOSITION(5),
        IME_ACKNOWLEDGE_FOCUS(6);

        public final int value;

        private ImeAction(int value) {
            this.value = value;
        }
    }

    public static final int IME_RANGE_CARETPOSITION = 1;
    public static final int IME_RANGE_RAWINPUT = 2;
    public static final int IME_RANGE_SELECTEDRAWTEXT = 3;
    public static final int IME_RANGE_CONVERTEDTEXT = 4;
    public static final int IME_RANGE_SELECTEDCONVERTEDTEXT = 5;

    public static final int IME_RANGE_LINE_NONE = 0;
    public static final int IME_RANGE_LINE_DOTTED = 1;
    public static final int IME_RANGE_LINE_DASHED = 2;
    public static final int IME_RANGE_LINE_SOLID = 3;
    public static final int IME_RANGE_LINE_DOUBLE = 4;
    public static final int IME_RANGE_LINE_WAVY = 5;

    public static final int IME_RANGE_UNDERLINE = 1;
    public static final int IME_RANGE_FORECOLOR = 2;
    public static final int IME_RANGE_BACKCOLOR = 4;
    public static final int IME_RANGE_LINECOLOR = 8;

    public static final int ACTION_MAGNIFY_START = 11;
    public static final int ACTION_MAGNIFY = 12;
    public static final int ACTION_MAGNIFY_END = 13;

    private final int mType;
    private int mAction;
    private boolean mAckNeeded;
    private long mTime;
    private Point[] mPoints;
    private int[] mPointIndicies;
    private int mPointerIndex; // index of the point that has changed
    private float[] mOrientations;
    private float[] mPressures;
    private Point[] mPointRadii;
    private Rect mRect;
    private double mX;
    private double mY;
    private double mZ;

    private int mMetaState;
    private int mFlags;
    private int mKeyCode;
    private int mUnicodeChar;
    private int mBaseUnicodeChar; // mUnicodeChar without meta states applied
    private int mRepeatCount;
    private int mCount;
    private int mStart;
    private int mEnd;
    private String mCharacters;
    private String mCharactersExtra;
    private String mData;
    private int mRangeType;
    private int mRangeStyles;
    private int mRangeLineStyle;
    private boolean mRangeBoldLine;
    private int mRangeForeColor;
    private int mRangeBackColor;
    private int mRangeLineColor;
    private Location mLocation;
    private Address mAddress;
    private DomKeyLocation mDomKeyLocation;

    private double mBandwidth;
    private boolean mCanBeMetered;

    private int mNativeWindow;

    private short mScreenOrientation;

    private ByteBuffer mBuffer;

    private int mWidth;
    private int mHeight;

    private GoannaEvent(NativeGoannaEvent event) {
        mType = event.value;
    }

    public static GoannaEvent createAppBackgroundingEvent() {
        return new GoannaEvent(NativeGoannaEvent.APP_BACKGROUNDING);
    }

    public static GoannaEvent createAppForegroundingEvent() {
        return new GoannaEvent(NativeGoannaEvent.APP_FOREGROUNDING);
    }

    public static GoannaEvent createNoOpEvent() {
        return new GoannaEvent(NativeGoannaEvent.NOOP);
    }

    public static GoannaEvent createKeyEvent(KeyEvent k, int metaState) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.KEY_EVENT);
        event.initKeyEvent(k, metaState);
        return event;
    }

    public static GoannaEvent createCompositorCreateEvent(int width, int height) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.COMPOSITOR_CREATE);
        event.mWidth = width;
        event.mHeight = height;
        return event;
    }

    public static GoannaEvent createCompositorPauseEvent() {
        return new GoannaEvent(NativeGoannaEvent.COMPOSITOR_PAUSE);
    }

    public static GoannaEvent createCompositorResumeEvent() {
        return new GoannaEvent(NativeGoannaEvent.COMPOSITOR_RESUME);
    }

    private void initKeyEvent(KeyEvent k, int metaState) {
        mAction = k.getAction();
        mTime = k.getEventTime();
        // Normally we expect k.getMetaState() to reflect the current meta-state; however,
        // some software-generated key events may not have k.getMetaState() set, e.g. key
        // events from Swype. Therefore, it's necessary to combine the key's meta-states
        // with the meta-states that we keep separately in KeyListener
        mMetaState = k.getMetaState() | metaState;
        mFlags = k.getFlags();
        mKeyCode = k.getKeyCode();
        mUnicodeChar = k.getUnicodeChar(mMetaState);
        // e.g. for Ctrl+A, Android returns 0 for mUnicodeChar,
        // but Goanna expects 'a', so we return that in mBaseUnicodeChar
        mBaseUnicodeChar = k.getUnicodeChar(0);
        mRepeatCount = k.getRepeatCount();
        mCharacters = k.getCharacters();
        mDomKeyLocation = isJoystickButton(mKeyCode) ? DomKeyLocation.DOM_KEY_LOCATION_JOYSTICK
                                                     : DomKeyLocation.DOM_KEY_LOCATION_MOBILE;
    }

    /**
     * This method tests if a key is one of the described in:
     * https://bugzilla.mozilla.org/show_bug.cgi?id=756504#c0
     * @param keyCode int with the key code (Android key constant from KeyEvent)
     * @return true if the key is one of the listed above, false otherwise.
     */
    private static boolean isJoystickButton(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_DPAD_LEFT:
            case KeyEvent.KEYCODE_DPAD_RIGHT:
            case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_UP:
                return true;
            default:
                if (Build.VERSION.SDK_INT >= 12) {
                    return KeyEvent.isGamepadButton(keyCode);
                }
                return GoannaEvent.isGamepadButton(keyCode);
        }
    }

    /**
     * This method is a replacement for the the KeyEvent.isGamepadButton method to be
     * compatible with Build.VERSION.SDK_INT < 12. This is an implementantion of the
     * same method isGamepadButton available after SDK 12.
     * @param keyCode int with the key code (Android key constant from KeyEvent).
     * @return True if the keycode is a gamepad button, such as {@link #KEYCODE_BUTTON_A}.
     */
    private static boolean isGamepadButton(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_BUTTON_C:
            case KeyEvent.KEYCODE_BUTTON_X:
            case KeyEvent.KEYCODE_BUTTON_Y:
            case KeyEvent.KEYCODE_BUTTON_Z:
            case KeyEvent.KEYCODE_BUTTON_L1:
            case KeyEvent.KEYCODE_BUTTON_R1:
            case KeyEvent.KEYCODE_BUTTON_L2:
            case KeyEvent.KEYCODE_BUTTON_R2:
            case KeyEvent.KEYCODE_BUTTON_THUMBL:
            case KeyEvent.KEYCODE_BUTTON_THUMBR:
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BUTTON_MODE:
            case KeyEvent.KEYCODE_BUTTON_1:
            case KeyEvent.KEYCODE_BUTTON_2:
            case KeyEvent.KEYCODE_BUTTON_3:
            case KeyEvent.KEYCODE_BUTTON_4:
            case KeyEvent.KEYCODE_BUTTON_5:
            case KeyEvent.KEYCODE_BUTTON_6:
            case KeyEvent.KEYCODE_BUTTON_7:
            case KeyEvent.KEYCODE_BUTTON_8:
            case KeyEvent.KEYCODE_BUTTON_9:
            case KeyEvent.KEYCODE_BUTTON_10:
            case KeyEvent.KEYCODE_BUTTON_11:
            case KeyEvent.KEYCODE_BUTTON_12:
            case KeyEvent.KEYCODE_BUTTON_13:
            case KeyEvent.KEYCODE_BUTTON_14:
            case KeyEvent.KEYCODE_BUTTON_15:
            case KeyEvent.KEYCODE_BUTTON_16:
                return true;
            default:
                return false;
        }
    }

    public static GoannaEvent createNativeGestureEvent(int action, PointF pt, double size) {
        try {
            GoannaEvent event = new GoannaEvent(NativeGoannaEvent.NATIVE_GESTURE_EVENT);
            event.mAction = action;
            event.mCount = 1;
            event.mPoints = new Point[1];

            PointF goannaPoint = new PointF(pt.x, pt.y);
            goannaPoint = GoannaAppShell.getLayerView().convertViewPointToLayerPoint(goannaPoint);

            if (goannaPoint == null) {
                // This could happen if Goanna isn't ready yet.
                return null;
            }

            event.mPoints[0] = new Point(Math.round(goannaPoint.x), Math.round(goannaPoint.y));

            event.mX = size;
            event.mTime = System.currentTimeMillis();
            return event;
        } catch (Exception e) {
            // This can happen if Goanna isn't ready yet
            return null;
        }
    }

    /**
     * Creates a GoannaEvent that contains the data from the MotionEvent.
     * The keepInViewCoordinates parameter can be set to false to convert from the Java
     * coordinate system (device pixels relative to the LayerView) to a coordinate system
     * relative to goanna's coordinate system (CSS pixels relative to goanna scroll position).
     */
    public static GoannaEvent createMotionEvent(MotionEvent m, boolean keepInViewCoordinates) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.MOTION_EVENT);
        event.initMotionEvent(m, keepInViewCoordinates);
        return event;
    }

    private void initMotionEvent(MotionEvent m, boolean keepInViewCoordinates) {
        mAction = m.getActionMasked();
        mTime = (System.currentTimeMillis() - SystemClock.elapsedRealtime()) + m.getEventTime();
        mMetaState = m.getMetaState();

        switch (mAction) {
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE:
            case MotionEvent.ACTION_HOVER_EXIT: {
                mCount = m.getPointerCount();
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
                mPointerIndex = m.getActionIndex();
                for (int i = 0; i < mCount; i++) {
                    addMotionPoint(i, i, m, keepInViewCoordinates);
                }
                break;
            }
            default: {
                mCount = 0;
                mPointerIndex = -1;
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
            }
        }
    }

    private void addMotionPoint(int index, int eventIndex, MotionEvent event, boolean keepInViewCoordinates) {
        try {
            PointF goannaPoint = new PointF(event.getX(eventIndex), event.getY(eventIndex));
            if (!keepInViewCoordinates) {
                goannaPoint = GoannaAppShell.getLayerView().convertViewPointToLayerPoint(goannaPoint);
            }

            mPoints[index] = new Point(Math.round(goannaPoint.x), Math.round(goannaPoint.y));
            mPointIndicies[index] = event.getPointerId(eventIndex);
            // getToolMajor, getToolMinor and getOrientation are API Level 9 features
            if (Build.VERSION.SDK_INT >= 9) {
                double radians = event.getOrientation(eventIndex);
                mOrientations[index] = (float) Math.toDegrees(radians);
                // w3c touchevents spec does not allow orientations == 90
                // this shifts it to -90, which will be shifted to zero below
                if (mOrientations[index] == 90)
                    mOrientations[index] = -90;

                // w3c touchevent radius are given by an orientation between 0 and 90
                // the radius is found by removing the orientation and measuring the x and y
                // radius of the resulting ellipse
                // for android orientations >= 0 and < 90, the major axis should correspond to
                // just reporting the y radius as the major one, and x as minor
                // however, for a radius < 0, we have to shift the orientation by adding 90, and
                // reverse which radius is major and minor
                if (mOrientations[index] < 0) {
                    mOrientations[index] += 90;
                    mPointRadii[index] = new Point((int)event.getToolMajor(eventIndex)/2,
                                                   (int)event.getToolMinor(eventIndex)/2);
                } else {
                    mPointRadii[index] = new Point((int)event.getToolMinor(eventIndex)/2,
                                                   (int)event.getToolMajor(eventIndex)/2);
                }
            } else {
                float size = event.getSize(eventIndex);
                Resources resources = GoannaAppShell.getContext().getResources();
                DisplayMetrics displaymetrics = resources.getDisplayMetrics();
                size = size*Math.min(displaymetrics.heightPixels, displaymetrics.widthPixels);
                mPointRadii[index] = new Point((int)size,(int)size);
                mOrientations[index] = 0;
            }
            mPressures[index] = event.getPressure(eventIndex);
        } catch (Exception ex) {
            Log.e(LOGTAG, "Error creating motion point " + index, ex);
            mPointRadii[index] = new Point(0, 0);
            mPoints[index] = new Point(0, 0);
        }
    }

    private static int HalSensorAccuracyFor(int androidAccuracy) {
        switch (androidAccuracy) {
        case SensorManager.SENSOR_STATUS_UNRELIABLE:
            return GoannaHalDefines.SENSOR_ACCURACY_UNRELIABLE;
        case SensorManager.SENSOR_STATUS_ACCURACY_LOW:
            return GoannaHalDefines.SENSOR_ACCURACY_LOW;
        case SensorManager.SENSOR_STATUS_ACCURACY_MEDIUM:
            return GoannaHalDefines.SENSOR_ACCURACY_MED;
        case SensorManager.SENSOR_STATUS_ACCURACY_HIGH:
            return GoannaHalDefines.SENSOR_ACCURACY_HIGH;
        }
        return GoannaHalDefines.SENSOR_ACCURACY_UNKNOWN;
    }

    public static GoannaEvent createSensorEvent(SensorEvent s) {
        int sensor_type = s.sensor.getType();
        GoannaEvent event = null;

        switch(sensor_type) {

        case Sensor.TYPE_ACCELEROMETER:
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_ACCELERATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case 10 /* Requires API Level 9, so just use the raw value - Sensor.TYPE_LINEAR_ACCELEROMETER*/ :
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_LINEAR_ACCELERATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case Sensor.TYPE_ORIENTATION:
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_ORIENTATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case Sensor.TYPE_GYROSCOPE:
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_GYROSCOPE;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = Math.toDegrees(s.values[0]);
            event.mY = Math.toDegrees(s.values[1]);
            event.mZ = Math.toDegrees(s.values[2]);
            break;

        case Sensor.TYPE_PROXIMITY:
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_PROXIMITY;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = 0;
            event.mZ = s.sensor.getMaximumRange();
            break;

        case Sensor.TYPE_LIGHT:
            event = new GoannaEvent(NativeGoannaEvent.SENSOR_EVENT);
            event.mFlags = GoannaHalDefines.SENSOR_LIGHT;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            break;
        }
        return event;
    }

    public static GoannaEvent createLocationEvent(Location l) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.LOCATION_EVENT);
        event.mLocation = l;
        return event;
    }

    public static GoannaEvent createIMEEvent(ImeAction action) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_EVENT);
        event.mAction = action.value;
        return event;
    }

    public static GoannaEvent createIMEKeyEvent(KeyEvent k) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_KEY_EVENT);
        event.initKeyEvent(k, 0);
        return event;
    }

    public static GoannaEvent createIMEReplaceEvent(int start, int end,
                                                   String text) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_EVENT);
        event.mAction = ImeAction.IME_REPLACE_TEXT.value;
        event.mStart = start;
        event.mEnd = end;
        event.mCharacters = text;
        return event;
    }

    public static GoannaEvent createIMESelectEvent(int start, int end) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_EVENT);
        event.mAction = ImeAction.IME_SET_SELECTION.value;
        event.mStart = start;
        event.mEnd = end;
        return event;
    }

    public static GoannaEvent createIMECompositionEvent(int start, int end) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_EVENT);
        event.mAction = ImeAction.IME_UPDATE_COMPOSITION.value;
        event.mStart = start;
        event.mEnd = end;
        return event;
    }

    public static GoannaEvent createIMERangeEvent(int start,
                                                 int end, int rangeType,
                                                 int rangeStyles,
                                                 int rangeLineStyle,
                                                 boolean rangeBoldLine,
                                                 int rangeForeColor,
                                                 int rangeBackColor,
                                                 int rangeLineColor) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.IME_EVENT);
        event.mAction = ImeAction.IME_ADD_COMPOSITION_RANGE.value;
        event.mStart = start;
        event.mEnd = end;
        event.mRangeType = rangeType;
        event.mRangeStyles = rangeStyles;
        event.mRangeLineStyle = rangeLineStyle;
        event.mRangeBoldLine = rangeBoldLine;
        event.mRangeForeColor = rangeForeColor;
        event.mRangeBackColor = rangeBackColor;
        event.mRangeLineColor = rangeLineColor;
        return event;
    }

    public static GoannaEvent createDrawEvent(Rect rect) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.DRAW);
        event.mRect = rect;
        return event;
    }

    public static GoannaEvent createSizeChangedEvent(int w, int h, int screenw, int screenh) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.SIZE_CHANGED);
        event.mPoints = new Point[2];
        event.mPoints[0] = new Point(w, h);
        event.mPoints[1] = new Point(screenw, screenh);
        return event;
    }

    public static GoannaEvent createBroadcastEvent(String subject, String data) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.BROADCAST);
        event.mCharacters = subject;
        event.mCharactersExtra = data;
        return event;
    }

    public static GoannaEvent createViewportEvent(ImmutableViewportMetrics metrics, DisplayPortMetrics displayPort) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.VIEWPORT);
        event.mCharacters = "Viewport:Change";
        StringBuffer sb = new StringBuffer(256);
        sb.append("{ \"x\" : ").append(metrics.viewportRectLeft)
          .append(", \"y\" : ").append(metrics.viewportRectTop)
          .append(", \"zoom\" : ").append(metrics.zoomFactor)
          .append(", \"fixedMarginLeft\" : ").append(metrics.marginLeft)
          .append(", \"fixedMarginTop\" : ").append(metrics.marginTop)
          .append(", \"fixedMarginRight\" : ").append(metrics.marginRight)
          .append(", \"fixedMarginBottom\" : ").append(metrics.marginBottom)
          .append(", \"displayPort\" :").append(displayPort.toJSON())
          .append('}');
        event.mCharactersExtra = sb.toString();
        return event;
    }

    public static GoannaEvent createURILoadEvent(String uri) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "";
        return event;
    }

    public static GoannaEvent createWebappLoadEvent(String uri) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "-webapp";
        return event;
    }

    public static GoannaEvent createBookmarkLoadEvent(String uri) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "-bookmark";
        return event;
    }

    public static GoannaEvent createVisitedEvent(String data) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.VISITED);
        event.mCharacters = data;
        return event;
    }

    public static GoannaEvent createNetworkEvent(double bandwidth, boolean canBeMetered) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.NETWORK_CHANGED);
        event.mBandwidth = bandwidth;
        event.mCanBeMetered = canBeMetered;
        return event;
    }

    public static GoannaEvent createThumbnailEvent(int tabId, int bufw, int bufh, ByteBuffer buffer) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.THUMBNAIL);
        event.mPoints = new Point[1];
        event.mPoints[0] = new Point(bufw, bufh);
        event.mMetaState = tabId;
        event.mBuffer = buffer;
        return event;
    }

    public static GoannaEvent createScreenOrientationEvent(short aScreenOrientation) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.SCREENORIENTATION_CHANGED);
        event.mScreenOrientation = aScreenOrientation;
        return event;
    }

    public static GoannaEvent createCallObserverEvent(String observerKey, String topic, String data) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.CALL_OBSERVER);
        event.mCharacters = observerKey;
        event.mCharactersExtra = topic;
        event.mData = data;
        return event;
    }

    public static GoannaEvent createRemoveObserverEvent(String observerKey) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.REMOVE_OBSERVER);
        event.mCharacters = observerKey;
        return event;
    }

    public static GoannaEvent createLowMemoryEvent(int level) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.LOW_MEMORY);
        event.mMetaState = level;
        return event;
    }

    public static GoannaEvent createNetworkLinkChangeEvent(String status) {
        GoannaEvent event = new GoannaEvent(NativeGoannaEvent.NETWORK_LINK_CHANGE);
        event.mCharacters = status;
        return event;
    }

    public void setAckNeeded(boolean ackNeeded) {
        mAckNeeded = ackNeeded;
    }
}
