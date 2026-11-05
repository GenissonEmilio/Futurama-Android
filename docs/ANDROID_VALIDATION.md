# Regressões de saves, memória, gráficos e desempenho

A validação do APK público 0.2.0 é registrada em [RELEASE_0.2.0.md](RELEASE_0.2.0.md).
As medições abaixo são o histórico da primeira rodada de otimização a 640×480.

## Execução local (05/11/2026)

As cinco suítes nativas foram compiladas com NDK 30.0.16248370, CMake 3.31.6,
ARM64/API 26, RelWithDebInfo, e executadas no aparelho Android API 36 conectado.
Não precisam de ISO ou de outros dados comerciais.

- `portable_runtime`: conversões numéricas e caminhos.
- `SaveStorage`: persistência/reabertura binária, substituição por dados menores,
  recuperação de transações interrompidas, erros reais de filesystem, rollback,
  cópia/exclusão e nova tentativa no mesmo handle.
- `ResourceLifetime`: referências de cliente e bindings, relação superfície/textura,
  purga/destruição dos managers e 16.384 buffers criados e liberados.
- `FilesystemSafety`: arquivos sintéticos válidos e malformados, strings sem NUL,
  diretórios, truncamentos, ciclos, offsets e tamanhos excessivos.
- `android_gles`: readback de pixels em EGL/GLES real, troca/reutilização de FBO,
  64 ciclos de render target com exclusão verificada por `glIsTexture` e
  `glIsRenderbuffer`, 16 atualizações de vídeo e mudanças de sampler, 4.096 draws
  para esgotar/reutilizar a arena de uniformes, mudança de frame e draws maiores
  que as capacidades iniciais de vértices/índices, sem erro GL.

As suítes de saves e filesystem também passaram com AddressSanitizer e
UndefinedBehaviorSanitizer no aparelho (`detect_leaks=0`; LeakSanitizer não foi usado).

O CI Linux executa as cinco suítes portáteis, incluindo `TextureConversion`.
Os testes GLES e raster requerem Android.
Exemplo, após configurar `SDK` para o SDK Android local:

```sh
cmake -S . -B build/android-tests -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$SDK/ndk/30.0.16248370/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFUT_BUILD_TESTS=ON
cmake --build build/android-tests --target fut_portable_tests fut_SaveStorage_tests \
  fut_ResourceLifetime_tests fut_FilesystemSafety_tests fut_TextureConversion_tests \
  fut_android_gles_tests fut_android_raster_tests -j4
python tools/run_android_tests.py --build-dir build/android-tests \
  --libcxx "$SDK/ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
```

No Windows, os executáveis do CMake/Ninja estão em `SDK/cmake/3.31.6/bin`.
O script aceita `--adb` e `--serial`. Ele usa exclusivamente
`/data/local/tmp/futurama-tests`; os saves instalados não são tocados.
São oito execuções: os sete executáveis e a variante raster `--scaled`.
`TextureConversion` usa texels e metadados XBE sintéticos, sem dados comerciais.
Os testes raster conferem pixels de depth, alpha, culling, render-to-texture,
texels zerados, mips, LOD bias e borda nativa. Também verificam os conectores
transparentes dos contornos em strips flat, com perspectiva e cauda de 404 vértices.
O teste GLES mantém 14.400 desenhos diferentes observáveis em três targets,
atravessando reutilizações de UBO/VBO/IBO e mudanças de frame antes do readback.
Esse teste detectou corrupção que o teste anterior de desenhos idênticos não detectava.

## Alterações e limites

Saves são gravados em staging, sincronizados, instalados por renomeação com backup
e recuperados na abertura seguinte. A conclusão de sucesso só é enfileirada após
persistir. O layout público dos diretórios permanece compatível. Falhas de I/O
seguem o caminho de erro já existente no gerenciador de cartões.

Payloads de recursos são devolvidos ao allocator após a última referência/binding;
as entradas dos caches GPU são excluídas na thread de render. Descritores pequenos
continuam como tombstones para preservar os handles brutos guardados pelo engine.
Portanto, metadados ainda crescem com o total histórico de criações. A busca por
buffers usa somente intervalos vivos, em O(log n).

A leitura inteira de um arquivo tem orçamento de 256 MiB; filmes maiores continuam
usando a interface de streaming. Imagens com índices/extensões inválidos são rejeitadas.

GLES reaproveita configurações de FBO e sampler, armazenamento de texturas e o buffer
de conversão de vídeo. Uma mudança de tamanho/níveis ou exclusão invalida o cache.
Uploads de uniformes, vértices e índices usam três bancos de streaming com VAOs
próprios, regiões distintas e alinhamento consultado para UBO. Cada banco recebe
um fence ao encerrar seus desenhos. O banco só é rebobinado ou redimensionado após
a GPU confirmar a conclusão; armazenamento em uso não é descartado por orphan.
As alocações são sob demanda (36 MiB nas capacidades iniciais dos três bancos),
com orçamento total de 60 MiB e espera quando todos estão ocupados.
O teste de desenhos diferentes reproduziu a corrupção da versão anterior e
passou com os bancos protegidos por fences.
APKs Android Debug usam `-O2` mantendo símbolos. Para depuração sem otimização,
configure `-DFUT_ANDROID_OPTIMIZE_DEBUG=OFF` no CMake.

## Como medir

A instalação de validação usa `-PfuturamaApplicationId=com.genisson.futuramarecomp.validation`
e `-PfuturamaAppLabel="Futurama (testes)"`.
O package original e seus saves são preservados. Nenhuma ISO, save, chave ou log
pessoal faz parte dos artefatos versionados.

```sh
adb shell setprop debug.futurama.profile 1
adb shell setprop debug.futurama.level Level1-1
adb shell setprop debug.futurama.skip_movies 1
# Reinicie o processo do jogo. O nível direto só é usado com o profiling ativado.
python tools/android_benchmark.py --seconds 45 --output artifacts/gameplay.json
adb shell setprop debug.futurama.profile 0
adb shell setprop debug.futurama.skip_movies 0
adb shell setprop debug.futurama.level ""
```

`skip_movies` permite repetir a cena sem aguardar os vídeos; só funciona com o
profiling ativado. Reinicie o processo após mudar essas propriedades.

`FuturamaPerf` registra grupos de 300 frames: FPS de apresentação, tempo médio/p95
de `App::Frame` (inclui espera de apresentação), CPU da thread do jogo por frame
e draws. CPU não inclui a thread de áudio. O script registra PSS/RSS do processo
do jogo. Compare a mesma cena jogável, descartando carregamentos e vídeos, com
o mesmo compilador/otimização e temperatura semelhante. FPS de apresentação não
é a frequência da simulação, que continua com passos de 60 Hz.

Compilar, executar suítes, renderizar menus e jogar são verificações distintas.
Esses testes não validam a campanha inteira, escuta do áudio, controle físico ou
durabilidade contra falha física do armazenamento. O backend D3D11 recebeu o mesmo
hook de liberação, mas não foi compilado/executado nesta máquina sem SDK Windows.

## Resultado da comparação no aparelho

POCO M8 Pro 5G / Adreno 810, API 36, alvo interno 640x480. Referência `2e4879c`
e versão corrigida compiladas com o mesmo NDK, `RelWithDebInfo`/`-O2`, ISO e ponto
inicial de Planet Express (`Level1-1`), sem movimento e com cerca de 726 draws por
frame. Profiling, entrada direta no nível e omissão dos vídeos ativados nas duas
versões. Coleta de 45 segundos em cada uma, após estabilizar o gameplay; bateria
em aproximadamente 35–36 °C. Estatísticas de FPS agregam janelas de 300 frames.

| Medida | Referência | Corrigido |
| --- | ---: | ---: |
| FPS agregado | 12,5 | 53,1 |
| Variação das janelas de FPS | 12,3–12,7 | 50,2–59,4 |
| CPU da thread do jogo por frame | 73,3 ms | 13,9 ms |
| PSS mediano do processo | 394,9 MiB | 286,7 MiB |

O ganho de FPS foi de aproximadamente 4,3 vezes nessa amostra; a CPU por frame
caiu cerca de 81%. PSS é uma observação do sistema, sensível a caches, paginação
e compartilhamento de bibliotecas. Esses números não representam toda a campanha.

O primeiro conjunto de caches de FBO/sampler/textura não melhorou o gameplay
significativamente. Um perfil de CPU com simpleperf (18.485 amostras, nenhuma
perdida) mostrou o driver Adreno alocando/liberando memória nos uploads por draw.
As arenas de streaming removeram essa realocação frequente. Simulação, áudio,
resolução interna e quantidade de desenhos foram preservados.

O APK final foi instalado como **Futurama (testes)** no package separado. Planet
Express foi renderizado, movimento e câmera responderam ao toque; HOME e retorno
pelo launcher conservaram o mesmo processo de jogo, sem erro EGL/nativo observado.
O aplicativo original permaneceu na versão 0.1.1 com seus dados privados.

APK local validado: `artifacts/Futurama-validation.apk`, SHA-256
`f8e3e639d5da9c7984c80085f75aceb2626448a57ebe81b0b5be9211a95cafa0`.
