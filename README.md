# Futurama Android

Port nativo experimental para Android do [Futurama: The Game: The Recomp](https://github.com/hiburn8/Futurama-The-Game-The-Recomp), de hiburn8. Mantém o código e o histórico do projeto original; as adaptações Android estão em `GenissonEmilio/Futurama-Android`.

Você precisa da sua própria ISO de **Futurama para Xbox**, NTSC-U ou PAL. Os dados do jogo não são incluídos no código nem no APK. A imagem de PS2 não é compatível.

## Estado

Implementados launcher com seletor de ISO, integração JNI, controles por toque/controle físico, renderização OpenGL ES 3 e áudio AAudio sobre o pipeline original de VP/DSP. O APK foi compilado e instalado em um Android ARM64: ISO, abertura, vídeos, menu, retorno do segundo plano e início jogável de Planet Express com movimento e câmera por toque foram verificados. A campanha completa ainda não foi validada; esta versão é experimental.

Consulte [a situação dos testes](docs/ANDROID_STATUS.md) e [a documentação original](docs/UPSTREAM_README.md).

## Requisitos e uso

- Android 8.0 (API 26) ou posterior, ARM64 e OpenGL ES 3.
- Espaço livre para uma cópia da ISO nos dados do aplicativo.
- Abra o app, selecione sua ISO de Xbox e aguarde a importação e validação. O app reutiliza a cópia local nas próximas aberturas.
- Os saves ficam nos dados privados do aplicativo. Desinstalar o app apaga esses dados.
- Controles na tela: dois analógicos, direcional, A/B/X/Y, gatilhos e Start/Back. Controles físicos Android também são aceitos.

## Compilar

JDK 17, Android SDK 36, Build Tools 35.0.0, NDK **30.0.16248370** e CMake **3.31.6**. Configure `sdk.dir` em `android/local.properties` (arquivo local, não versionado).

O Gradle Wrapper fixa Gradle 8.12 e verifica seu SHA-256. No diretório `android`:

```sh
./gradlew assembleDebug
```

No Windows, use `gradlew.bat assembleDebug`. APK: `android/app/build/outputs/apk/debug/app-debug.apk`. Não é necessário extrair ou incorporar a ISO para compilar.

Testes do núcleo em host Linux:

```sh
cmake -S . -B build/tests -DFUT_BUILD_TESTS=ON
cmake --build build/tests --target fut_portable_tests -j2
ctest --test-dir build/tests --output-on-failure
```

## Organização

| Área | Implementação |
| --- | --- |
| Launcher, ISO e toque | `android/app/src/main/java/com/genisson/futuramarecomp/MainActivity.java` |
| Ciclo de vida, JNI e entrada | `src/platform/android/AndroidGame.*`, `AndroidInput.*` |
| Renderização EGL/GLES3 | `src/render/gles/` |
| Áudio AAudio e filmes | `src/platform/android/AndroidSoundBackend.*`, `MovieService.*` |
| Lógica original | `src/game/`, `src/engine/`, `src/audio/`, `src/video/` |

O alvo Windows e o backend Direct3D 11 continuam no CMake para comparação.

## Origem

Base: commit `d6ec3dfc50da6206906fe1a7c0fb517f3287a606` do projeto de hiburn8 (v1.5.0). As anotações `orig:` e os créditos foram preservados. O upstream não contém arquivo de licença; este repositório não acrescenta uma licença sobre código de terceiros. Futurama e os dados originais pertencem aos respectivos titulares.
