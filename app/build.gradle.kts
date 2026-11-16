plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "io.github.leukoplast1980.mecrisppocket"
    compileSdk = 36
    ndkVersion = "27.0.12077973"  // pinned for reproducible (F-Droid) builds

    defaultConfig {
        applicationId = "io.github.leukoplast1980.mecrisppocket"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    // Release signing comes from the environment (the CI workflow fills it
    // from repository secrets). Without SIGNING_KEYSTORE the release APK is
    // left unsigned, which is what F-Droid's build server expects.
    val keystore = System.getenv("SIGNING_KEYSTORE")
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = file(keystore)
                storePassword = System.getenv("SIGNING_STORE_PASSWORD")
                keyAlias = System.getenv("SIGNING_KEY_ALIAS")
                keyPassword = System.getenv("SIGNING_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            // Installs next to the release build, with its own data.
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (keystore != null) signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/pocketAssets"))
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("io.github.amrdeveloper:codeview:1.3.9")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}

// Assembles the Mecrisp-Quintus core for the board and packs it as an asset.
// Needs GNU binutils for RISC-V (Debian/Ubuntu: binutils-riscv64-linux-gnu).
val buildPocketImage by tasks.registering(Exec::class) {
    val outDir = layout.buildDirectory.dir("generated/pocketAssets")
    inputs.dir(rootProject.file("quintus"))
    inputs.file(rootProject.file("scripts/build-core.sh"))
    inputs.file(rootProject.file("scripts/build-image.sh"))
    outputs.dir(outDir)
    commandLine("sh", rootProject.file("scripts/build-image.sh").path, outDir.get().asFile.path)
}

tasks.named("preBuild") {
    dependsOn(buildPocketImage)
}
