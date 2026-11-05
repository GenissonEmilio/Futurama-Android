# Controles por toque

Os controles mudam pelo personagem realmente controlado, sem botão para trocar
personagens. Vida, munição e inventário continuam vindo do HUD original do jogo.
Os ícones dos controles são vetoriais: contornos claros, metal, ciano e badges
com as cores A/B/X/Y, sem carregar texturas adicionais no renderizador GLES.

| Personagem | A | X | B | Outros |
| --- | --- | --- | --- | --- |
| Fry | Ataque da arma equipada | Golpe alternativo ou carregar disparo | Pular | Arma anterior/próxima; mirar com arma de fogo; visão em primeira pessoa |
| Bender | Investida/ataque | Giro | Pular | Especial, centralizar câmera, visão |
| Leela | Chute | Soco | Pular | Especial, esquiva, visão; sequências A/X mantêm os combos originais |
| Zoidberg | — | — | Pular | Movimento e Y em uma interface simplificada |

O botão Especial de Bender/Leela envia **A+X** juntos. Cargas, disponibilidade e
combos continuam sob controle do jogo. No Fry, segurar X carrega a arma de fogo
e soltar dispara quando essa arma suporta a ação; não foi criada uma recarga
manual inexistente no mapeamento padrão. Com as mãos vazias, os ícones são
genéricos, sem sugerir uma arma equipada.

## Câmera

Mova pelo analógico esquerdo. Arraste na área livre do lado direito para olhar,
sem um segundo analógico fixo. Arrastar para a direita olha para a direita;
arrastar para cima olha para cima. A direção horizontal é compensada ao entrar
na primeira pessoa, pois o jogo original usa sinais opostos nesses modos.

O dedo da câmera, o dedo de movimento e os dedos de ação têm donos independentes:
um não rouba o outro ao cruzar regiões. Manter o dedo parado não mantém o analógico
da câmera acionado. Cancelar o toque, perder foco ou mudar de personagem solta
os comandos. Alterar arma/contexto preserva o movimento e solta ações que sumiram.

Na engrenagem, escolha **Ajustar câmera** para mudar sensibilidade e inverter
cada eixo; as opções são salvas. Ocultar os controles mantém pausa e engrenagem
acessíveis. No trecho de Zoidberg montado, a câmera continua guiada pelo jogo;
o HUD não apresenta um controle de câmera que esse modo ignora.

## Y contextual

Y permanece utilizável e muda conforme os avisos **visíveis** do próprio jogo:
porta, mecanismo ou interação genérica. A classificação usa os IDs estáveis dos
avisos e o glifo Xbox Y, não nomes de objetos próximos ou uma suposição de distância.
Avisos ocultos, removidos, expirados ou transparentes não ativam o destaque.
Sem indicação específica, o botão mantém **Interagir**. Não se inventa uma ação
"Examinar" onde o jogo não fornece uma indicação confiável.

## Validação

- Testes Java: `python tools/run_touch_tests.py` (JDK 17), com três dedos, IDs
  não sequenciais, cancelamentos, resize, perfis e áreas seguras da tela.
- Testes nativos: `AndroidInput` testa acumulação/consumo, sinais, deadzones,
  concorrência e mescla com controle físico; `TouchHudState` testa tipos,
  estados de jogo e o ciclo real dos avisos HUD usando fontes/strings sintéticas.
- Instrumentação Android sem AndroidX: `assembleDebugAndroidTest`, executada
  com `TouchControlsInstrumentation`; envia MotionEvents reais ao View e verifica
  saída, inversão, três dedos, mudança de arma/contexto/personagem e opções.
- Teste no aparelho: perfis dos quatro personagens, arrasto nos dois eixos,
  primeira pessoa e pausa. Estes testes curtos não validam toda a campanha.
