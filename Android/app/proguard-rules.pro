# SameBoy Android R8/ProGuard rules.
#
# The native library binds JNI by NAME (Java_io_sameboy_android_NativeBridge_native*), so
# R8 must not rename or strip NativeBridge or any native method — that would break the JNI
# lookup at runtime. Keep the bridge class and all native-method-bearing classes intact.
-keep class io.sameboy.android.NativeBridge { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}

# Activities/Application/providers are kept via the manifest automatically. Everything else
# in the tiny Java UI layer may be shrunk/optimized freely.
