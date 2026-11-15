plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "io.github.leukoplast1980.mecrisppocket"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.github.leukoplast1980.mecrisppocket"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
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
