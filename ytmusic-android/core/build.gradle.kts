plugins {
    id("org.jetbrains.kotlin.jvm")
    application
}

java {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
        // keep bytecode simple for Android's D8 on old devices
        freeCompilerArgs.addAll("-Xstring-concat=inline", "-Xlambdas=class", "-Xsam-conversions=class")
    }
}

dependencies {
    api("com.squareup.okhttp3:okhttp:4.12.0")
    compileOnly("org.json:json:20240303")   // Android has org.json built in; the jar is only needed to compile and for the PC test
    api("com.github.teamnewpipe:NewPipeExtractor:v0.26.5")
}

application {
    mainClass.set("dev.ytmusic.core.SmokeKt")
}

tasks.named<JavaExec>("run") { classpath += configurations.compileClasspath.get() }
