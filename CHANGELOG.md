# Changelog do Majoras Mask Android Definitive Edition

Mudanças deste fork (lokodoshape-png/2ship2harkinian-Android) em relação ao 2 Ship 2 Harkinian original.
As builds saem na página [Releases](https://github.com/lokodoshape-png/2ship2harkinian-Android/releases):
`pc-4.0-build-N` para Windows e Linux e `android-4.0-build-N` para Android.

O jogo não vem com a ROM nem com assets da Nintendo. Você extrai os seus da sua própria cópia.

## Base 4.0.2 (branch `mm3d-4.0`)

### 2026-11-04 — versão 4.0.2-mm3d.4
- **Tela inicial refeita com telas próprias.** O menu não entra mais no jogo para mostrar nada: cada item abre uma
  tela própria, com título, caminho (Menu > Configurações > ...), botão Voltar, indicador de página, ajuda do item
  selecionado, dicas de botão e transição. B, o botão Voltar ou Backspace sempre voltam uma tela.
  - **Iniciar Jogo**: pede confirmação, abre a entrada de nome do jogo no primeiro arquivo vazio e começa o jogo assim
    que o nome é confirmado.
  - **Continuar**: carrega o último arquivo jogado.
  - **Carregar Jogo** e **Arquivos de Jogo**: os arquivos com nome, corações, máscaras, restos dos chefes, canções,
    rupias, dia e hora e ciclos de três dias, lidos dos saves. Jogar, criar jogo novo num arquivo vazio e excluir com
    confirmação (os mesmos passos do Apagar do jogo). O save do MM não guarda tempo de jogo, então a tela mostra os
    ciclos.
  - **Configurações**: abas Vídeo, Resolução, Áudio, Idioma, Controles, Acessibilidade e Definitive Edition, com
    opções reais aplicadas na hora, e "Todas as opções" para o menu completo.
  - **Controles**: todos os botões e o que fazem, as combinações, e as opções de controle com o editor de botões.
  - **Extras**: galeria, artes conceituais, informações da edição, conteúdos adicionais, música do menu e a abertura
    original.
  - **Créditos**: equipe da Definitive Edition, desenvolvedores originais e colaboradores do 2 Ship 2 Harkinian.
  - **Sair**: pergunta e fecha o jogo.
- Navegação por controle, teclado e toque: analógico ou direcional movem, A ou Start abrem, B volta, L/Z e R trocam
  de página; setas, Enter, Backspace e Page Up/Down no teclado.
- Saem do menu: Bombers Book, Missões e Inventário (eles entravam no jogo). O caderno, o menu de missões e o
  inventário continuam no jogo como antes.
- O gameplay, os menus internos e os controles dentro do jogo não mudam.
- Arquivos: `mm/2s2h/Enhancements/TitleMenu/TitleMenu.cpp`, `TitleMenu.h`, `make_assets.py`, os novos
  `mm/assets/custom/overlays/titlemenu/` (`icon_hourglass`, `icon_gamepad`, `icon_door`, `label_start`,
  `label_files`, `label_settings`, `label_controls`, `label_quit`, `concept_1..3`), `Android/app/build.gradle`
  (versionName), este CHANGELOG.

### 2026-11-04 — versão 4.0.2-mm3d.3
- **Tela inicial: os controles de toque ficam na tela.** O analógico, o direcional, o A e o B da tela agora mexem no
  menu como um controle. Um toque fora deles vai direto para o item do menu, em vez de girar a câmera. Com o botão do
  olho, os controles somem e a tela toda fica para o toque.
- Arquivos: `mm/2s2h/Enhancements/TitleMenu/TitleMenu.cpp`, `Android/app/src/main/java/com/dishii/mm/MainActivity.java`,
  `Android/app/build.gradle` (versionName), este CHANGELOG.

### 2026-11-04 — versão 4.0.2-mm3d.2
- **Tela inicial: o toque funciona.** Os toques e cliques agora são lidos direto da tela, e a tela inicial fica sempre
  na frente da janela do jogo, que antes podia ficar por cima e engolir os toques.
- **Dicas de botão conforme o controle.** Com controle (Xbox, PlayStation, Nintendo) ou teclado, o rodapé mostra os
  botões do aparelho em uso. Depois de um toque, mostra "Toque numa opção para escolher" e um botão **Voltar** para
  tocar, já que os botões de toque ficam escondidos na tela inicial. As dicas de A e B também aceitam toque e clique.
- Junto, na mesma versão, o menu de missões novo do branch `mm3d-4.0`.
- Arquivos: `mm/2s2h/Enhancements/TitleMenu/TitleMenu.cpp`, `Android/app/build.gradle` (versionName), este CHANGELOG.

### 2026-11-04 — versão 4.0.2-mm3d.1
- **Tela inicial nova (menu principal).** Substitui a tela de título ("Press Start") e a tela principal da seleção de
  arquivos por uma tela feita a partir da arte do Rafael, com o fundo, os ícones e a moldura de seleção tirados da
  imagem e o subtítulo DEFINITIVE EDITION sob o logo. Itens:
  - **Continuar** carrega o último arquivo jogado (ou o primeiro que existir).
  - **Novo Jogo** pede confirmação e abre a entrada de nome do jogo no primeiro arquivo vazio; os outros saves não
    são tocados. Sem arquivo vazio, avisa para apagar um em Carregar Jogo.
  - **Carregar Jogo** mostra a seleção de arquivos original (corações, máscaras, dia, save da coruja, copiar e
    apagar). B nela volta para a tela nova.
  - **Bombers Book**, **Missões** e **Inventário** carregam o último arquivo e abrem, já no jogo, o Caderno dos
    Bombers (pela página de missões do menu de pausa, com os dados do save), o menu de missões ou a página de itens do
    menu de pausa. Se o save ainda não tem o caderno, o jogo avisa e abre a página de missões.
  - **Opções** abre o menu de configurações (gráficos, resolução, áudio, controles e o resto).
  - **Extras**: música do menu, opções de som e mira do jogo original, abertura original, créditos e versão.
  - Música de fundo da trilha original: **Tema de Majora** por padrão; em Extras dá para trocar por Últimas Horas,
    Torre do Relógio, Castelo de Ikana ou a música da seleção original.
  - Controle (Bluetooth ou não: direcional, analógico, A / Start confirma, B volta), teclado (setas, Enter, Backspace
    e as teclas do jogo), mouse e toque. No Android os botões de toque somem enquanto a tela está aberta, para o toque
    chegar nos itens, e voltam no jogo e na seleção de arquivos original.
  - Efeitos leves: a lua flutua e "respira", os olhos dela e a Tatl brilham, partículas sobem no ar, a seleção pulsa e
    desliza, os painéis entram com transição. Desligáveis em Melhorias > Economia de tempo > Cenas > "Efeitos da tela inicial".
  - Funciona em qualquer proporção de tela: a arte se ajusta à tela e uma cópia desfocada preenche as sobras.
  - Liga e desliga em Melhorias > Economia de tempo > Cenas > "Tela inicial nova"; desligada, volta tudo como no jogo original.
  - Arquivos novos: `mm/2s2h/Enhancements/TitleMenu/` (`TitleMenu.cpp`, `TitleMenu.h`, `make_assets.py`) e
    `mm/assets/custom/overlays/titlemenu/` (arte). Alterados: `z_opening.c` e `z_file_choose_NES.c` (dois novos
    `VB_START_TITLE_SCREEN` e `VB_FILE_SELECT_EXIT_TO_TITLE`), `GameInteractor_VanillaBehavior.h`, `BenGui.cpp`,
    `BenMenu.cpp`, `Android/app/build.gradle` (versionName).
  - O versionCode do Android continua 40 de propósito: subir ele faz o app apagar o `mm.o2r` e pedir a ROM de novo.
    O `2ship.o2r` com a arte nova é trocado em qualquer atualização.

- **Menu de missões dentro do jogo.** Com o jogo pausado, aperte N (teclado), Back / View / Select / - (controle) ou o
  botão MISSÕES no canto da tela de pausa. O menu de pausa fica parado por trás e o próprio controle do jogo navega:
  L / Z e R trocam a categoria, o direcional ou o analógico escolhem, A abre os detalhes, B volta e Start fecha. Mouse
  e toque funcionam em tudo. O botão "Abrir o menu de missões" em Melhorias > Majora's Mask 3D pausa o jogo e abre.
  - Sete categorias: missões principais, secundárias, as 24 máscaras, personagens, locais (estátuas da coruja), itens
    importantes e registros (canções, fadas perdidas, Skulltulas, corações).
  - Cada missão tem região, horário, descrição, objetivos marcados, recompensa e um dos quatro estados: bloqueada,
    disponível, em andamento ou concluída. Tudo é lido das variáveis do save; um passo que o jogo não registra só
    aparece marcado quando a missão inteira está concluída.
  - Arte própria em `assets/custom/overlays/questmenu/` (molduras, pergaminho, botões, ícones de categoria e de
    estado), gerada por `make_assets.py`. Os ícones das missões são os do seu jogo, então um pacote HD troca eles também.
  - Código em `mm/2s2h/Enhancements/QuestMenu/`: `QuestData` (o que é listado), `QuestState` (estado de cada missão),
    `QuestRenderer` (desenho) e `QuestMenu` (entrada, layout e animações). O menu de pausa para com o novo
    `VB_KALEIDO_UPDATE`.
  - A janela antiga do guia saiu; as dicas em notas dentro do Caderno dos Bombers continuam.
  - Visual de livro aberto como na arte conceitual: moldura roxa entalhada, título com emblema, teclas L/R, dia e
    hora, lista com retratos e caixas de marcar em páginas, e o progresso de cada missão em quadrinhos com o ícone do
    jogo para cada passo (A e depois o direcional percorrem os passos). Fonte Alegreya (licença OFL, em
    `assets/custom/fonts`).

### 2026-11-03
- **MM3D: estátuas da coruja salvam e o jogo continua** (em vez de voltar à tela de título), e o save fica guardado.
- **MM3D: Goron rola sem segurar A.** Aperte A para rolar e A de novo para parar.
- **MM3D: a canção do espantalho não some** quando a Canção do Tempo volta ao primeiro dia.
- **MM3D: Grandes Fadas trocadas.** A de Woodfall dobra a magia e a de Snowhead dá o Giro Especial (as falas não mudam).
- **MM3D: Koume e Dampé trocados.** O tiro ao alvo da Koume dá a garrafa e o baú do Dampé dá o Pedaço de Coração.
- **MM3D: Máscara do Gigante na luta contra o Twinmold.** Ela chega quando o primeiro Twinmold cai; o baú do Templo
  da Torre de Pedra que a guardava dá uma rupia roxa.
- **MM3D: bolhas do Deku miradas em primeira pessoa**, com a câmera do arco.
- **MM3D: Canção do Tempo Invertida na metade da velocidade** (no N64 é um terço).
- **MM3D: estátua da coruja acorda só de examinar**, sem precisar bater com a espada.
- **PC: pacote HD mais leve.** A escolha entre a textura HD e a de um mod carregado depois agora é guardada, em vez de
  recalculada para cada textura em cada quadro.
- **Menos travadinhas: os shaders ficam salvos no disco** (DirectX 11, Vulkan e OpenGL). Cada efeito novo ainda compila
  uma vez, mas nas próximas vezes que você abrir o jogo ele já está pronto. Ficam na pasta `shadercache`.
- **Caderno dos Bombers em forma de menu de missões.** A janela do guia agora tem seções (Pessoas, Máscaras, Canções,
  Fadas Perdidas), a lista com o que já foi feito marcado e os detalhes de cada um: progresso, horário, local e
  recompensa. Usa os retratos do próprio Caderno e os ícones do jogo.
  Todas ficam em Melhorias > Majora's Mask 3D.
- **PC com o nome e o ícone novos.** Título da janela, ícone do `.exe` (Windows) e do AppImage (Linux), e arquivos do
  release `Majoras-Mask-Android-Definitive-Edition-windows/linux-<commit>.zip`.
- **Novo nome: Majoras Mask Android Definitive Edition.** Nome do app, do arquivo APK e ícone novos. A pasta de dados no
  celular passa a ser `Majoras Mask Android Definitive Edition`; na primeira abertura o app move para ela tudo o que estava
  em `2S2H-MM3D` (mm.o2r, saves, mods, traduções).

### 2026-10-29
- **Android: cada build novo instala por cima do anterior.** Antes cada build era assinado com uma chave diferente e o
  Android recusava a atualização ("conflito com um pacote já existente"). Na primeira vez com esta versão é preciso
  desinstalar o app antigo; a pasta `2S2H-MM3D` fica no celular.
- **Android: controle físico navega nos menus.** Os menus liam só o primeiro controle, que era o virtual dos botões de
  toque; agora leem todos. Ao usar um controle físico os botões de toque somem sozinhos e voltam ao tocar na tela.
- **Android: menu do 2Ship sem os botões de toque por cima.** Enquanto o menu está aberto os botões de toque somem, então
  todo toque vai para o menu.
- **Relógio estilo MM3D: número do dia (1, 2, 3) de volta embaixo da barra**, com o dia atual aceso.
- **Android: barra de porcentagem ao criar o `mm.o2r`.** Mostra quantos arquivos já foram extraídos e avisa quando está
  gravando o arquivo final.
- **Android: menu reformulado para o celular.** O tamanho da interface agora segue a densidade da tela (antes era sempre
  3x) e pode ser escolhido em Configurações > Geral > Tamanho da interface. As abas de cima usam uma fonte menor
  quando não cabem, a barra lateral se ajusta ao nome mais longo, o texto usa uma fonte proporcional e as colunas viram
  uma só quando ficariam estreitas demais.
- **Tradução e texturas HD juntas.** Com "Usar assets alternativos" ligado, as texturas HD (alt/) de um pacote como o MM
  Reloaded passavam por cima de qualquer mod, inclusive da tradução, e menus e telas voltavam ao inglês. Agora um mod
  carregado depois do pacote HD ganha nas texturas que ele mesmo troca.
- **Android: registro da abertura em `2S2H-MM3D/inicio.log`.** Registra cada etapa da inicialização desde o primeiro
  instante, e qualquer fechamento (Java ou nativo) com o rastro. Ajuda a achar problemas que acontecem antes do log do
  jogo começar.
- **Android: correção do app fechando ao criar o `mm.o2r`** depois de atualizar de uma versão antiga. O app agora troca
  o `2ship.o2r` e os arquivos do extrator a cada atualização, mesmo com a permissão de "todos os arquivos" do Android 11+.
  Antes ficavam os arquivos da versão antiga, e o extrator novo fechava ao ler esses arquivos.
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
