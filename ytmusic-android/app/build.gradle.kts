plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "dev.ytmusic"
    compileSdk = 34

    defaultConfig {
        applicationId = "dev.ytmusic.player"
        minSdk = 22      // Fire OS 5 and newer
        targetSdk = 30   // Fire OS 8 is Android 11; avoids newer-Android permission / FGS-type hurdles
        versionCode = 19
        versionName = "1.18"
    }

    // The signing key is not part of the repository (anyone holding it can publish updates that install over
    // the app). Without it the release build falls back to the debug key.
    val keystore = rootProject.file("ytmusic.keystore")
    signingConfigs {
        if (keystore.exists()) create("release") {
            storeFile = keystore
            storePassword = "ytmusic"
            keyAlias = "ytmusic"
            keyPassword = "ytmusic"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
        isCoreLibraryDesugaringEnabled = true
    }
    kotlinOptions {
        jvmTarget = "11"
        freeCompilerArgs += listOf("-Xstring-concat=inline", "-Xlambdas=class", "-Xsam-conversions=class")
    }
    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
    packaging {
        resources.excludes += setOf("META-INF/DEPENDENCIES", "META-INF/LICENSE*", "META-INF/NOTICE*", "META-INF/*.version", "DebugProbesKt.bin", "**/*.kotlin_builtins", "**/*.kotlin_metadata")
    }
}

dependencies {
    implementation(project(":core"))
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.media3:media3-exoplayer:1.4.1")
    implementation("androidx.media3:media3-session:1.4.1")
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs_nio:2.1.2")
}
