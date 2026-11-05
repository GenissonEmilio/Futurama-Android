# Validação da release 0.2.0

05/11/2026. APK ARM64 assinado, versão `0.2.0` / código `20`, pacote
`com.genisson.futurama.android`. Android 8/API 26 ou superior e GLES 3.

## Correções

- Saves com staging, sincronização, backup recuperável, rollback e erros propagados.
- Liberação de payloads CPU e caches GPU após a última referência/binding;
  superfícies e managers liberam os recursos que possuem.
- Leitores de arquivos validam nomes, limites, ciclos e orçamentos de memória.
- Texturas RGB24 e máscaras de canais convertidas corretamente; atualizações
  para texels zerados preservam preto e alpha transparente.
- `MIPMAPLODBIAS`, `MAXMIPLEVEL` e borda de sampler nativa quando suportada.
- Supersampling padrão 2× no alvo principal (1280×960), mantendo lógica 640×480
  e proporção 4:3. Render targets auxiliares mantêm seus tamanhos próprios.
- Corrupção intermitente dos contornos/HUD corrigida: três bancos VBO/IBO/UBO
  distintos, com fences, só são reutilizados após a GPU terminar. Sem descarte
  de armazenamento ainda em uso. Alocação sob demanda, orçamento total de 60 MiB.
- Ícone original vetorial adaptive, com PNGs normal/round de mdpi a xxxhdpi.

## Validações executadas

Build completo nativo **Release/-O3**, NDK 30.0.16248370, CMake 3.31.6,
ARM64/API 26; empacotamento Gradle/JDK 17/SDK 36. Os mesmos objetos nativos
foram usados nas suítes e no APK assinado instalado no aparelho.

Oito execuções nativas passaram no POCO M8 Pro 5G / Adreno 810 / API 36:

1. Núcleo portátil: conversões numéricas e caminhos.
2. Saves: persistência, reabertura, recuperação, falhas de I/O e nova tentativa.
3. Recursos: referências/bindings, superfícies, managers e 16.384 buffers.
4. Filesystem: arquivos sintéticos válidos, truncados e malformados.
5. Texturas: formatos canônicos, RGB24, máscaras, stride, mips e BC1/2/3.
6. GLES: exclusão GPU, vídeo, samplers, draws grandes e **14.400 desenhos
   diferentes** retidos até o readback, atravessando bancos e mudanças de frame.
7. Raster: depth, alpha, culling, RTT, uploads zerados, mip/LOD/borda, strips flat
   e conectores transparentes dos contornos com perspectiva e 404 vértices.
8. Raster 2×: tamanho físico de cor/depth, viewport e scissor.

A variante adicional `--reinit` também passou, recriando o contexto EGL.
O teste de 14.400 desenhos falhou no streaming anterior e passou na correção.
Saves/filesystem também passaram anteriormente com ASan/UBSan no Android
(`detect_leaks=0`). As suítes usam dados sintéticos; não precisam da ISO.

No APK assinado: importação da ISO pelo seletor, reaproveitamento da cópia local
e início de Planet Express verificados. Movimento e câmera responderam ao toque.
Uma sequência de 24 capturas ao longo de aproximadamente 36 segundos não mostrou
a corrupção intermitente do retrato HUD que aparecia em 9/24 capturas do caminho
anterior. Esta é uma amostragem visual, além dos testes de pixels; não comprova
ausência de todo defeito gráfico.
Os cenários iniciais de `Level2-1` (Bender) e `Level3-1` (Leela) também foram
renderizados e inspecionados. HOME e retorno ao aplicativo mantiveram o mesmo
processo e a imagem em gameplay, sem erro EGL/nativo observado. Não foram
concluídas essas fases nem validada a progressão entre elas.

## Desempenho do APK assinado

Coleta de 45 segundos, ponto inicial de Planet Express (`Level1-1`), sem movimento,
após carregamento, com profiling/entrada direta/omissão de vídeos ativados.
Nove janelas de 300 frames, aproximadamente 726 draws/frame, alvo 1280×960.

| Medida | Release 0.2.0 |
| --- | ---: |
| FPS agregado das janelas | 58,1 |
| Variação de FPS por janela | 52,2–61,0 |
| CPU média da thread do jogo/frame | 13,5 ms |
| PSS mediano do processo | 282,5 MiB |

O resultado anterior de 53,1 FPS era um build RelWithDebInfo/-O2 a 640×480;
a referência anterior às otimizações mediu 12,5 FPS nessa cena. As condições e
compilação diferem entre rodadas: os números não isolam o ganho de cada alteração
nem representam desempenho sustentado ou toda a campanha. A simulação mantém 60 Hz.

## Pacote e instalação

APK: `Futurama-Android-0.2.0-arm64.apk` (5.154.301 bytes).
SHA-256: `a2411f644700126ff3f8e53a626ec7b8735d51b472686a0e47aeeda72720d6d1`.

Assinaturas APK v2/v3 e alinhamento ZIP de 16 KiB verificados. Certificado público
SHA-256: `9d40e34b1fa239252cca7b80f5c0e3f1a8419d5ce95d02ba86f7e5c5cb29b974`.
O APK contém o runtime ARM64 e o ícone; a inspeção rejeita dados do disco, saves,
chaves e logs privados. A chave de assinatura permanece fora do repositório.

O pacote público separado preserva a instalação antiga, cuja assinatura não está
disponível. A ISO e os saves não são migrados automaticamente entre pacotes.
Importe sua própria ISO Xbox pelo seletor do aplicativo. O APK não inclui o jogo.

## Limites conhecidos

Esta é uma versão experimental; compilar, abrir, testar menus e jogar são
verificações diferentes. A campanha completa, controles físicos, escuta do áudio
e equivalência visual completa com Xbox/Windows não foram validados.
Os shaders NV2A programáveis, bump-env, vertex blending e point sprites mantêm
limitações herdadas do backend original. Sem extensão de borda nativa, usa-se
clamp-to-edge. BC é expandido para RGBA8 na CPU. Pequenos descritores tombstone
permanecem para preservar handles; seu total acompanha o histórico de criações.
As alterações D3D11 não foram compiladas/executadas nesta máquina.

Procedimentos reproduzíveis: [ANDROID_VALIDATION.md](ANDROID_VALIDATION.md).
