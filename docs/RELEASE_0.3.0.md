# Validação da release 0.3.0

05/11/2026. APK ARM64 assinado, versão `0.3.0` / código `30`, pacote
`com.genisson.futurama.android`. Android 8/API 26 ou superior e GLES 3.

## Controles

- HUD de toque acompanha o personagem real: Fry, Bender, Leela ou Zoidberg.
  Sem botão de alternar personagens. Vida, inventário e munição vêm do jogo.
- Ícones vetoriais de arma, martelo, soco, chute, giro, especial e interação;
  botões circulares e cores A/B/X/Y. Sem novas texturas no renderizador GLES.
- Analógico de movimento e câmera independente por arrasto na área direita livre.
  A correção de deadzone permite arrastos lentos; o delta é consumido uma vez.
  Primeira pessoa compensa o sinal horizontal diferente do jogo original.
- Três dedos independentes para movimento, câmera e ação. Cancelamento, perda
  de foco e mudança de personagem soltam a entrada; mudanças de arma/contexto
  preservam movimento e soltam os botões que deixaram de existir.
- Sensibilidade e inversões salvas. Ocultar controles conserva pausa e ajustes.
- Y usa avisos efetivamente visíveis do HUD: porta, mecanismo ou Interagir.
  Sem aviso confiável, conserva Interagir. A classificação não inventa uma
  ação Examinar. O trecho montado de Zoidberg mantém a câmera guiada pelo jogo.

Mapeamentos e instruções: [TOUCH_CONTROLS.md](TOUCH_CONTROLS.md).
As correções de saves, recursos e gráficos da 0.2.0 foram preservadas.

## Validações executadas

Build nativo completo **Release/-O3**, NDK 30.0.16248370, CMake 3.31.6,
ARM64/API 26; empacotamento Gradle/JDK 17/SDK 36. Os objetos nativos do APK
também foram usados nas suítes executadas no aparelho.

- Dez execuções nativas passaram: núcleo portátil, saves, recursos, filesystem,
  texturas, AndroidInput, TouchHudState, GLES, raster e raster 2×. Incluem pixels,
  14.400 desenhos diferentes atravessando bancos GPU, sinais/deadzones da câmera,
  concorrência e lifecycle dos avisos HUD com dados sintéticos.
- **193 verificações Java** passaram: IDs não sequenciais, posse dos dedos,
  múltiplos toques, cancelamento, áreas seguras, resize, perfis e visibilidade.
- **71 verificações instrumentadas Android** passaram no View real com
  MotionEvents: movimento/câmera/ação simultâneos, reordenação de índices,
  cancelamento, sensibilidade/inversões, primeira pessoa, troca de personagem,
  arma e contexto; sumiço de um botão pressionado libera sua entrada.
- APK assinado instalado como atualização da versão pública 0.2.0, com a mesma
  assinatura e reaproveitamento da ISO privada existente.
- Trechos iniciais dos quatro personagens renderizados e inspecionados no
  celular: Fry (`Level1-1`), Bender (`Level2-1`), Leela (`Level3-1`) e Zoidberg
  (`Level4-1`), com controles correspondentes e sem alternador de personagens.
- Arrasto horizontal e vertical verificados visualmente: direita olha à direita,
  cima olha para cima. Arrasto horizontal também verificado em primeira pessoa.
- Pausa abre o menu original; configurações da câmera e ocultação/restauração
  dos controles verificadas. HOME e retorno preservaram o mesmo processo e a
  imagem de gameplay, sem erro EGL/nativo observado.

A semântica de porta/mecanismo foi validada pelo estado nativo e ciclo real dos
avisos com dados sintéticos; não foi concluída uma sequência de portas/mecanismos
da campanha. Especiais continuam dependendo das cargas e regras originais.

## Desempenho do APK assinado

POCO M8 Pro 5G / Adreno 810 / Android API 36. Coleta de 45 segundos após carregar
Planet Express, sem movimento, com profiling/entrada direta/omissão de vídeos
ativados. Nove janelas de 300 frames, cerca de 726 draws/frame, alvo 1280×960.

| Medida | Release 0.3.0 |
| --- | ---: |
| FPS agregado das janelas | 58,0 |
| Variação de FPS por janela | 54,3–62,5 |
| CPU média da thread do jogo/frame | 13,6 ms |
| PSS mediano do processo | 257,4 MiB |

A versão 0.2.0 mediu 58,1 FPS e 13,5 ms de CPU nessa cena e resolução. O resultado
permaneceu próximo nesta amostra; coletas curtas não isolam o custo dos controles
nem comprovam desempenho sustentado. PSS varia com caches, paginação e bibliotecas
compartilhadas. A frequência da simulação permanece em 60 Hz.

## Pacote

APK: `Futurama-Android-0.3.0-arm64.apk` (5.178.877 bytes).
SHA-256: `8809c5273d4c0f594d5a7f89fb3a8c399989cf6988f047f8019c28b459a86577`.

Assinaturas APK v2/v3, certificado e alinhamento ZIP de 16 KiB verificados.
Certificado público SHA-256:
`9d40e34b1fa239252cca7b80f5c0e3f1a8419d5ce95d02ba86f7e5c5cb29b974`.
Inspeção do APK confirmou runtime/ícone e ausência de dados do disco, saves,
chaves e logs privados. Chave de assinatura e dados usados localmente não são
publicados. O APK exige uma ISO Xbox própria.

Atualizar a versão pública 0.2.0 preserva o pacote e seus dados. A instalação
antiga com outro package continua separada; dados não migram entre pacotes.

## Limites

Versão experimental. Compilar, abrir, testar menus e jogar são verificações
distintas. Os testes de gameplay foram curtos; campanha completa, progressão,
controles físicos reais, escuta do áudio e equivalência visual integral ainda
não foram validados. Zoidberg foi verificado no trecho de câmera guiada.
As limitações do renderizador descritas na [0.2.0](RELEASE_0.2.0.md) permanecem;
o alvo Windows não foi compilado nesta rodada.

Procedimentos reproduzíveis: [ANDROID_VALIDATION.md](ANDROID_VALIDATION.md).
