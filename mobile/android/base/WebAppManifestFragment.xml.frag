        <activity android:name=".WebApps$WebApp@APPNUM@"
                  android:label="@string/webapp_generic_name"
                  android:configChanges="keyboard|keyboardHidden|mcc|mnc|orientation|screenSize"
                  android:windowSoftInputMode="stateUnspecified|adjustResize"
                  android:launchMode="singleTask"
                  android:taskAffinity="org.mozilla.goanna.WEBAPP@APPNUM@"
                  android:process=":@ANDROID_PACKAGE_NAME@.WebApp@APPNUM@"
                  android:excludeFromRecents="true"
                  android:theme="@style/Goanna.App">
            <intent-filter>
                <action android:name="org.mozilla.goanna.WEBAPP@APPNUM@" />
            </intent-filter>
            <intent-filter>
                <action android:name="org.mozilla.goanna.ACTION_ALERT_CALLBACK" />
            </intent-filter>
        </activity>

