# NewPipeExtractor + Rhino (see the NewPipe app's rules)
-keep class org.schabi.newpipe.extractor.timeago.patterns.** { *; }
-keep class org.mozilla.javascript.** { *; }
-keep class org.mozilla.classfile.ClassFileWriter
-dontwarn org.mozilla.javascript.JavaToJSONConverters
-dontwarn org.mozilla.javascript.tools.**
-dontwarn javax.script.**
-dontwarn java.beans.**
-dontwarn jdk.dynalink.**
-dontwarn org.jsoup.**
-keepattributes Signature,*Annotation*,InnerClasses,EnclosingMethod
-keep class dev.ytmusic.PlaybackService { *; }

# protobuf-lite messages are matched by field name at runtime (e.g. "browseId_"): don't rename them
-keep class org.schabi.newpipe.extractor.services.youtube.protos.** { *; }
-keep class org.schabi.newpipe.extractor.** extends com.google.protobuf.GeneratedMessageLite { *; }
-keepclassmembers class * extends com.google.protobuf.GeneratedMessageLite { <fields>; }
-keepclassmembers class * extends com.google.protobuf.GeneratedMessageLite$Builder { <fields>; }
-dontwarn com.google.protobuf.**
