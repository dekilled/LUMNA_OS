plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "os.lumna.remote"
    compileSdk = 35

    defaultConfig {
        applicationId = "os.lumna.remote"
        minSdk = 26
        targetSdk = 35
        versionCode = (System.getenv("LUMNA_VERSION_CODE") ?: "1").toInt()
        versionName = System.getenv("LUMNA_VERSION_NAME") ?: "0.1.0"
    }

    // Uma chave fixa deixa cada APK novo instalar por cima do anterior. O CI passa por
    // LUMNA_KEYSTORE_*; sem ela, fica a chave de debug da máquina (muda entre execuções do CI).
    val keystore = System.getenv("LUMNA_KEYSTORE_FILE")?.let(::file)?.takeIf { it.isFile }
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = keystore
                storePassword = System.getenv("LUMNA_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("LUMNA_KEY_ALIAS")
                keyPassword = System.getenv("LUMNA_KEY_PASSWORD") ?: System.getenv("LUMNA_KEYSTORE_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

dependencies {
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20250517")
}
