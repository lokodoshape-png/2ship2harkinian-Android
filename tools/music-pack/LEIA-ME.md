# Mod de músicas (2Ship MM3D)

Gera um mod `.o2r` a partir de uma pasta com as suas músicas (`.ogg`, `.mp3`, `.wav`, `.flac`, `.opus`, `.m4a`).
Use só gravações suas ou que você tem permissão para usar.

## Passo a passo

1. Instale o [Python 3](https://www.python.org/downloads/) e o [ffmpeg](https://ffmpeg.org/download.html)
   (no Windows: `winget install ffmpeg`). O `ffmpeg` precisa funcionar no terminal.
2. Coloque as músicas numa pasta, cada uma com o nome da música do jogo que ela substitui.
   Os nomes aceitos estão em `nomes-das-musicas.txt` (por exemplo `Clock Town Day 1.ogg`, `Termina Field.flac`).
   Maiúsculas, espaços e pontuação não importam, e o nome técnico (`NA_BGM_TERMINA_FIELD.ogg`) também vale.
3. Rode:

       python mm_music_pack.py "C:\caminho\da\pasta"

   Sai um `pasta.o2r` ao lado da pasta.
4. Copie o `.o2r` para a pasta `mods` do jogo
   (PC: ao lado do executável; Android: `/storage/emulated/0/2S2H-MM3D/mods`).

Pronto: as músicas com nome do jogo tocam no lugar das originais. Faixas com outros nomes aparecem no
Editor de Áudio (menu de melhorias) para você escolher onde tocar. Lá também dá para voltar à original.

## Loop

Por padrão a música inteira repete. Para repetir só a partir de um ponto (pulando a introdução),
crie um `loops.txt` dentro da pasta:

    Termina Field = 7.52
    Clock Town Day 1 = 3.1 - 92.4

Os números são segundos (`início` ou `início - fim`). As tags `LOOPSTART`/`LOOPEND` do arquivo também são lidas.
Fanfarras e músicas de ocarina tocam uma vez só, automaticamente.

Opções: `-o saida.o2r` escolhe o arquivo de saída, `--qualidade 0..10` a qualidade (padrão 6),
`--lista` mostra os nomes aceitos.
