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

## Em validação

- Build completo do APK e carregamento das bibliotecas no aparelho.
- ISO, abertura, vídeos, menu e primeira fase.
- Áudio real, controles simultâneos, retorno do background e reconexão de superfície.
- Desempenho sustentado e comparação de imagem com Windows.

Dados usados localmente nos testes não são publicados. O APK não contém ISO, XBE, texturas, filmes nem arquivos de som do disco.
