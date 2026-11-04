# Situação do port Android

Atualizado em 04/11/2026. Compilar um backend não comprova equivalência com o Xbox.

## Implementação

- ARM64/API 26: conversões x87/SSE com comportamento definido para NaN/overflow, alinhamento de heap e arquivos com offsets de 64 bits.
- EGL/GLES3: backend do fluxo D3D8 original, shaders de função fixa, texturas e render targets. Mantém as limitações do backend original para shaders NV2A programáveis, bump-env, vertex blending e point sprites; borda de sampler usa clamp.
- AAudio: saída estéreo do modelo original de vozes, VP e efeitos DSP; fila de áudio, recuperação de desconexão e suspensão.
- Launcher: importação de ISO pelo Storage Access Framework, validação Xbox/XBE, progresso e reaproveitamento local.
- Entrada: controles na tela, teclado e controles Android; suspensão e reconexão de Surface.

## Verificado

- Regressões numéricas e normalização de caminhos no host Windows: passaram.
- Fontes de áudio compiladas com NDK r30 para ARM64/API 26 com `-Wall -Wextra -Werror`.
- GLES3 compilado sintaticamente com NDK r30 para ARM64/API 26; shaders compilados e linkados por ANGLE/WebGL2 headless.
- Launcher Java compilado contra Android SDK 36 com JDK 17.
- Build completo ARM64/API 26 e APK de desenvolvimento: passaram; pacote sem dados do disco e alinhamento de 16 KB verificados.
- GitHub Actions: testes do núcleo em Linux e geração completa do APK passaram no commit `db54740`.
- Aparelho POCO M8 Pro 5G, Android API 36: instalação, regressões numéricas nativas, importação da ISO pelo seletor, reabertura com a ISO instalada, vídeos, menu principal, seleção de New Game e abertura de Planet Express.
- AAudio iniciou saída estéreo 48 kHz; os registros do sistema mostraram amostras não nulas. Qualidade sonora e equivalência ainda não foram aferidas por escuta.
- A orientação dos polígonos foi corrigida após o teste em aparelho revelar tela preta; o menu e os vídeos passaram a renderizar.

## Em validação

- Gameplay da primeira fase e das demais fases.
- Controles simultâneos, retorno do background e reconexão de superfície após as correções de ciclo de vida.
- Qualidade do áudio, saves e controles físicos.
- Desempenho sustentado e comparação de imagem com Windows.

Dados usados localmente nos testes não são publicados. O APK não contém ISO, XBE, texturas, filmes nem arquivos de som do disco.
