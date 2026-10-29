# Changelog do 2Ship MM3D

Mudanças deste fork (lokodoshape-png/2ship2harkinian-Android) em relação ao 2 Ship 2 Harkinian original.
As builds saem na página [Releases](https://github.com/lokodoshape-png/2ship2harkinian-Android/releases):
`pc-4.0-build-N` para Windows e Linux e `android-4.0-build-N` para Android.

O jogo não vem com a ROM nem com assets da Nintendo. Você extrai os seus da sua própria cópia.

## Base 4.0.2 (branch `mm3d-4.0`)

### 2026-10-29
- **Ícones de botão conforme o controle** (Configurações > Controles > Ícones dos botões). Os botões aparecem como os do
  controle usado por último (Xbox, PlayStation, Nintendo Switch ou teclado), e cada botão do N64 é mostrado como o botão
  a que está ligado no mapeamento.
  - Valem para os diálogos, os ícones do menu de pausa (A, C, Z e R), as notas da ocarina e uns ícones pequenos nos
    botões A, B e C do HUD (podem ser desligados).
  - "Automático" troca sozinho ao mudar de controle. "N64" mantém os ícones originais. Os ícones são desenhados pelo
    próprio jogo, sem nenhum asset de terceiros.
- **Aviso "Segure [botão] para acelerar"** no canto da tela durante as cenas, com o ícone do seu controle. Enquanto
  acelera, mostra "Acelerando". Pode ser desligado em Melhorias > Atalhos.
- **Textos da tradução PT-BR que o jogo monta sozinho:** "horas" na contagem da lua, os ordinais das fadas perdidas
  e das Skulltulas ("3a fada", "21o"), as cores das máscaras da Casa da Aranha e os nomes das estátuas da coruja.
- **Músicas no menu** (Melhorias > Editor de Áudio > Músicas). Em "Escolher pasta de músicas..." você aponta uma pasta
  com as suas faixas (wav, flac, mp3, ogg ou opus). O jogo converte tudo para a pasta `musicas` e mostra uma barra de progresso.
  - Uma faixa com o nome de uma música do jogo (por exemplo `Clock Town Day 1.flac`) toca no lugar dela.
    Há uma opção para ligar e desligar isso, e as faixas também aparecem no Editor de Áudio.
  - Um `loops.txt` na pasta define o ponto de loop, em segundos. Fanfarras e músicas de ocarina tocam uma vez só.
  - No Android a pasta é escolhida pelo seletor do sistema.
  - As músicas ficam em Opus, comprimidas na memória, então dá para usar uma trilha inteira sem pesar.
- **Músicas do jogo em MIDI** (Melhorias > Editor de Áudio > Músicas > "Exportar músicas do jogo (MIDI)"). Salva cada
  música do jogo na pasta `musicas-midi` como um `.mid`, já com o nome que a pasta de músicas reconhece.
  - Cada canal vira uma faixa com o nome do instrumento e um instrumento General MIDI parecido. A percussão vai no
    canal 10. O andamento é o do jogo.
  - Junto vai um `loops.txt` com os pontos de loop em segundos. As marcas `loopStart` e `loopEnd` também ficam no MIDI.
  - Os MIDIs saem da sua própria cópia do jogo e não vêm no download.
- **Exportar como mod (.o2r)** (na mesma tela). Junta as músicas convertidas, com nomes e loops, num único
  `Minhas musicas.o2r` na pasta `musicas-mod`. Serve para levar a outro aparelho ou compartilhar as suas gravações,
  e se instala em Configurações > Mods.

### 2026-10-28
- **Renderizador Vulkan (PC)**, escolhido como "Vulkan" na lista de renderizadores.
  - Mantém o cel shading, os contornos, o MSAA e a resolução interna.
  - Só aparece quando o PC tem suporte a Vulkan. O OpenGL continua sendo o padrão.
  - A build 81 corrigiu o fechamento ao abrir no Windows: o SDL da build de Windows estava sem Vulkan.
- **Dicas do Caderno dos Bombers dentro do caderno do jogo.**
  - Ficam separadas por personagem e por dia (Dia 1, Dia 2, Dia 3, Qualquer dia), com hora, lugar e o que fazer.
  - Cores: amarelo enquanto falta fazer, verde quando o caderno registra a recompensa, apagada quando o dia já passou.
  - Só aparecem depois que Link conhece o personagem.
- **Android na base 4.0.**
  - App próprio (`com.dishii.mm.mm3d`, pasta `2S2H-MM3D`) com controles na tela e giroscópio do celular.
  - O 2ship.o2r já vem dentro do APK. A extração da ROM usa o seletor de arquivos do Android, e as mensagens de configuração estão em português.
- **Menu de Mods** (Configurações > Mods): lista, liga, desliga e instala mods `.o2r`, `.otr` e `.zip`.
- **Idioma do jogo** (Configurações > Idioma do jogo): carrega um pacote de tradução depois dos mods. No Android ele é escolhido pelo seletor de arquivos.
- **Cel shading com contornos**, com brilho e saturação ajustáveis, no OpenGL e no DirectX 11.
- **Melhorias estilo Majora's Mask 3D** trazidas para a 4.0:
  - iluminação MM3D (névoa menor, luz ambiente e direcional, brilho e saturação);
  - Caderno dos Bombers logo na primeira visita;
  - variações da Canção do Tempo na tela de Status;
  - escolha da hora na Canção do Tempo Dobrado;
  - número do dia no relógio estilo MM3D;
  - janela de guia do Caderno dos Bombers.
- **Segurar B acelera as cenas.** As cenas rodam mais rápido sem pular nada do roteiro.
- **Menu do 2Ship em português do Brasil**, com a opção "Idioma do menu". A busca funciona nos dois idiomas.
- **CI:** builds de Windows e Linux publicadas como `pc-4.0-build-N` e APK publicado como `android-4.0-build-N`.

## Base anterior (branch `ci/android-apk`, PR #1)

Primeira versão do port, em cima do 2Ship anterior à 4.0. Tudo o que vale a pena dela foi trazido para a base 4.0 acima.

- 2026-10-27: cel shading estilo Budokai, B segurado avança diálogos, DirectX 11 como padrão no Windows,
  opções MM3D ligadas por padrão, correções de interpolação de quadros e menu em tela cheia com Idioma e Mods.
- 2026-10-25: workflow do APK Android, app separado do 2Ship original, 2ship.o2r no APK, escolha da hora na
  Canção do Tempo Dobrado, Caderno dos Bombers antecipado, variações da Canção do Tempo, guia do Caderno dos Bombers,
  dia no relógio, iluminação MM3D, menu traduzido e B segurado para acelerar cenas.
