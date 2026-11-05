# Situação do port Android

Atualizado em 05/11/2026. Compilar um backend não comprova equivalência com o Xbox.

## Versão 0.2.0

- Versão pública `0.2.0`, `versionCode 20`, pacote `com.genisson.futurama.android`. O pacote separado preserva a instalação anterior, cuja assinatura não está disponível; ISO e saves privados não são migrados automaticamente entre os pacotes.
- Ícone original da nave: foreground/background vetoriais adaptive e PNGs regular/round de mdpi a xxxhdpi.
- Alvo principal com supersampling padrão 2×: cor e profundidade em 1280×960, mantendo viewport e geometria nas coordenadas lógicas 640×480. Render targets auxiliares mantêm suas dimensões próprias.
- Conversão de texturas de 24 bits e máscaras de canais validada; uploads e atualizações preservam texels totalmente zerados, incluindo alpha transparente e RGB preto.
- Samplers respeitam `MIPMAPLODBIAS` e `MAXMIPLEVEL`. A cor de borda usa `GL_EXT_texture_border_clamp` ou `GL_OES_texture_border_clamp`; sem essas extensões, o fallback é clamp-to-edge.
- Oito execuções de suítes ARM64 passaram no Android: núcleo portátil, saves, recursos, filesystem, conversão de texturas, GLES, raster e raster `--scaled` (2×). A suíte raster verifica pixels e o tamanho físico de cor/profundidade; o caso de borda nativa é pulado quando a extensão não existe.
- O APK Release assinado passou nas oito suítes ARM64 e na recriação EGL adicional. O streaming usa três bancos protegidos por fences, com orçamento de 60 MiB; o teste de 14.400 desenhos diferentes reproduziu o erro do caminho anterior e passou com a correção.
- Medição de 45 segundos em Planet Express no POCO M8 Pro 5G / Adreno 810: aproximadamente 58 FPS agregados, com alvo principal em 1280×960. Os 53,1 FPS da rodada anterior eram a 640×480, em outro build; esta comparação curta não comprova desempenho sustentado na campanha.
- APK assinado instalado e importação/reabertura verificadas. Inícios de `Level1-1`, `Level2-1` e `Level3-1` inspecionados; movimento/câmera por toque e HOME/retorno verificados. A sequência final de 24 capturas não mostrou a corrupção intermitente anterior do retrato HUD.

## Histórico de regressões de 05/11/2026 (Debug anterior)

- Gravação de saves com staging, sincronização, backup recuperável e propagação de falhas.
- Payloads CPU e caches GPU liberados após a última referência/binding, incluindo superfícies e render targets.
- Leitores de arquivos com validação de limites, nomes, ciclos e orçamentos de memória.
- Cinco suítes ARM64 executadas com sucesso no aparelho API 36: núcleo portátil, saves, recursos, filesystem e GLES.
- Saves e filesystem também executados com AddressSanitizer e UndefinedBehaviorSanitizer, sem falhas; LeakSanitizer desativado nessa execução Android.
- Build completo Gradle/NDK Debug com otimização `-O2`: passou. APK de validação separado do aplicativo original; estrutura e alinhamento de 16 KB verificados.
- Comparação curta de Planet Express: 12,5 para 53,1 FPS agregados no mesmo ponto, após substituir uploads por draw por arenas de streaming; não representa a campanha inteira.
- APK Debug de validação: gameplay 3D, movimento e câmera por toque, HOME/retorno com o mesmo processo de jogo e sem erro EGL/nativo observado.

Validação do APK público: [RELEASE_0.2.0.md](RELEASE_0.2.0.md).
Procedimentos, limites e resultados anteriores: [ANDROID_VALIDATION.md](ANDROID_VALIDATION.md).

## Implementação

- ARM64/API 26: conversões x87/SSE com comportamento definido para NaN/overflow, alinhamento de heap e arquivos com offsets de 64 bits.
- EGL/GLES3: backend do fluxo D3D8 original, shaders de função fixa, texturas e render targets. Mantém as limitações do backend original para shaders NV2A programáveis, bump-env, vertex blending e point sprites. Borda de sampler nativa depende da extensão da GPU, com fallback clamp-to-edge.
- AAudio: saída estéreo do modelo original de vozes, VP e efeitos DSP; fila de áudio, recuperação de desconexão e suspensão.
- Launcher: importação de ISO pelo Storage Access Framework, validação Xbox/XBE, progresso e reaproveitamento local.
- Entrada: controles na tela, teclado e controles Android; suspensão e reconexão de Surface.

## Verificado

- Regressões numéricas e normalização de caminhos no host Windows: passaram.
- Fontes de áudio compiladas com NDK r30 para ARM64/API 26 com `-Wall -Wextra -Werror`.
- GLES3 compilado sintaticamente com NDK r30 para ARM64/API 26; shaders compilados e linkados por ANGLE/WebGL2 headless.
- Launcher Java compilado contra Android SDK 36 com JDK 17.
- Build completo ARM64/API 26 e APK de desenvolvimento: passaram; pacote sem dados do disco e alinhamento de 16 KB verificados.
- GitHub Actions: testes do núcleo em Linux e geração completa do APK passaram no commit de código `3904737` ([execução](https://github.com/GenissonEmilio/Futurama-Android/actions/runs/41823531092)).
- Aparelho POCO M8 Pro 5G, Android API 36: instalação, regressões numéricas nativas, importação da ISO pelo seletor, reabertura com a ISO instalada, vídeos, menu principal, seleção de New Game e abertura de Planet Express.
- AAudio iniciou saída estéreo 48 kHz; os registros do sistema mostraram amostras não nulas. Qualidade sonora e equivalência ainda não foram aferidas por escuta.
- A orientação dos polígonos foi corrigida após o teste em aparelho revelar tela preta; o menu e os vídeos passaram a renderizar.
- HOME e reabertura pelo ícone mantiveram a mesma sessão e processo, com imagem no menu e sem duplicação de atividades ou erro de superfície EGL.
- Planet Express chegou à parte jogável em 3D: Fry respondeu ao analógico esquerdo de toque e a câmera respondeu ao analógico direito. Este foi um teste curto de início da fase, sem concluir a missão.

## Em validação

- Conclusão da primeira fase, transições e campanha completa nas demais fases.
- Controles simultâneos e ciclos prolongados de suspensão/recriação de superfície.
- Qualidade do áudio, saves e controles físicos.
- Desempenho sustentado e comparação de imagem com Windows.

Dados usados localmente nos testes não são publicados. O APK não contém ISO, XBE, texturas, filmes nem arquivos de som do disco.

Artefato histórico de validação local (anterior à versão pública 0.2.0): `Futurama-Android-0.1.0-debug.apk`, versão `0.1.0`, SHA-256 `05d1d3d2fb1ae5b1f814b0a3969d4f9c8a688ed41e85ada53b9b13d014a964c1`. O APK gerado em outra máquina pode ter outra assinatura de desenvolvimento e outro hash.
