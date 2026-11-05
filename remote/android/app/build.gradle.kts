plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "os.lumna.remote"
    compileSdk = 35

    defaultConfig {
        applicationId = "os.lumna.connect"
        minSdk = 26
        targetSdk = 35
        // Sempre sobe (minutos desde 2026): qualquer build novo instala por cima do anterior, da CI ou daqui.
        versionCode = System.getenv("LUMNA_VERSION_CODE")?.toIntOrNull() ?: ((System.currentTimeMillis() / 60000) - 29_000_000).toInt()
        versionName = System.getenv("LUMNA_VERSION_NAME") ?: "0.1.0"
    }

    // Uma chave fixa deixa cada APK novo instalar por cima do anterior. O CI passa a sua por LUMNA_KEYSTORE_*;
    // fora do CI vale remote/android/lumna-connect.jks, que fica só na sua máquina (o git ignora). Sem nenhuma
    // das duas, o APK sai com a chave de debug (e para atualizar é preciso desinstalar a versão anterior).
    val envKeystore = System.getenv("LUMNA_KEYSTORE_FILE")?.let(::file)?.takeIf { it.isFile }
    val localKeystore = rootProject.file("lumna-connect.jks").takeIf { it.isFile }
    signingConfigs {
        if (envKeystore != null) {
            create("release") {
                storeFile = envKeystore
                storePassword = System.getenv("LUMNA_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("LUMNA_KEY_ALIAS")
                keyPassword = System.getenv("LUMNA_KEY_PASSWORD") ?: System.getenv("LUMNA_KEYSTORE_PASSWORD")
            }
        } else if (localKeystore != null) {
            create("release") {
                storeFile = localKeystore
                storePassword = System.getenv("LUMNA_KEYSTORE_PASSWORD") ?: "lumnaconnect"
                keyAlias = System.getenv("LUMNA_KEY_ALIAS") ?: "lumna"
                keyPassword = System.getenv("LUMNA_KEY_PASSWORD") ?: System.getenv("LUMNA_KEYSTORE_PASSWORD") ?: "lumnaconnect"
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
