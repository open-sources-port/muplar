// c++ -std=c++20 tests/android/apk_activity_alias.cpp -lz -o
// build/tests/apk_activity_alias
#include <cassert>
#include <iostream>
#include "../../platform/android-aarch64/apk/apk_envelope.cpp"

int main(int argc, char **argv)
{
    using namespace muplar::runtime::apk;
    const std::optional<std::string> package = "com.example.store";
    const std::string manifest = R"(<manifest><application>
        <activity android:name=".OtherActivity" />
        <activity android:name=".MainActivity" />
        <activity-alias android:name=".Launcher" android:targetActivity=".MainActivity">
            <intent-filter><action android:name="android.intent.action.MAIN" />
            <category android:name="android.intent.category.LAUNCHER" /></intent-filter>
        </activity-alias>
    </application></manifest>)";
    assert(infer_plain_manifest_launch_activity(manifest, package) ==
           "com.example.store.MainActivity");
    const std::string missing_target = R"(<manifest><application>
        <activity android:name=".OtherActivity" />
        <activity-alias android:name=".Missing">
            <intent-filter><action android:name="android.intent.action.MAIN" />
            <category android:name="android.intent.category.LAUNCHER" /></intent-filter>
        </activity-alias>
    </application></manifest>)";
    assert(infer_plain_manifest_launch_activity(missing_target, package) ==
           "com.example.store.OtherActivity");
    if (argc == 2) {
        auto apk = classify_apk(argv[1]);
        assert(apk.manifest_package == "com.android.vending");
        assert(apk.manifest_launch_activity ==
               "com.google.android.finsky.activities.MainActivity");
        std::cout
            << "Binary Play Store launcher alias resolves to MainActivity\n";
    }
    std::cout << "Activity alias regression tests passed\n";
}
