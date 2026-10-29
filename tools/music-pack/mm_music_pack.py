#!/usr/bin/env python3
"""Empacota uma pasta de músicas (ogg, mp3, wav, flac, opus, m4a) num mod .o2r para o 2Ship (MM3D).

Cada arquivo substitui a música do jogo que tem o mesmo nome, por exemplo "Clock Town Day 1.ogg"
ou "NA_BGM_CLOCK_TOWN_DAY_1.flac". Os nomes aceitos aparecem com --lista. O mod gerado vai na
pasta "mods" do jogo; as faixas também aparecem no Editor de Áudio para trocar à mão.

Precisa do ffmpeg e do ffprobe no PATH. Uso:
    python mm_music_pack.py PASTA_DAS_MUSICAS [-o orquestrada.o2r] [--loops loops.txt]

Loops: por padrão a música inteira repete. Para repetir só um trecho, crie um loops.txt com linhas
"Nome da música = inicio" ou "Nome da música = inicio - fim" (em segundos, ex. "Termina Field = 7.52").
As tags LOOPSTART/LOOPEND (em amostras) do próprio arquivo também são lidas.
Fanfarras e músicas de ocarina tocam uma vez só; outros nomes podem terminar em "_fanfare" para isso.
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

# Replaceable tracks from mm/2s2h/Enhancements/Audio/AudioCollection.cpp: (label, sfxKey, plays once).
TRACKS = [
    ("Termina Field", "NA_BGM_TERMINA_FIELD", False),
    ("Chase", "NA_BGM_CHASE", False),
    ("Majora's Theme", "NA_BGM_MAJORAS_THEME", False),
    ("Clock Tower", "NA_BGM_CLOCK_TOWER", False),
    ("Stone Tower Temple", "NA_BGM_STONE_TOWER_TEMPLE", False),
    ("Inverted Stone Tower Temple", "NA_BGM_INV_STONE_TOWER_TEMPLE", False),
    ("Missed Event 0", "NA_BGM_FAILURE_0", False),
    ("Missed Event 1", "NA_BGM_FAILURE_1", False),
    ("Happy Mask Salesman's Theme", "NA_BGM_HAPPY_MASK_SALESMAN", False),
    ("Song Of Healing", "NA_BGM_SONG_OF_HEALING", False),
    ("Southern Swamp", "NA_BGM_SWAMP_REGION", False),
    ("Alien Invasion", "NA_BGM_ALIEN_INVASION", False),
    ("Boat Cruise", "NA_BGM_SWAMP_CRUISE", False),
    ("Sharp's Curse", "NA_BGM_SHARPS_CURSE", False),
    ("Great Bay", "NA_BGM_GREAT_BAY_REGION", False),
    ("Ikana", "NA_BGM_IKANA_REGION", False),
    ("Deku Palace", "NA_BGM_DEKU_PALACE", False),
    ("Mountain Region", "NA_BGM_MOUNTAIN_REGION", False),
    ("Pirates Fortress", "NA_BGM_PIRATES_FORTRESS", False),
    ("Clock Town Day 1", "NA_BGM_CLOCK_TOWN_DAY_1", False),
    ("Clock Town Day 2", "NA_BGM_CLOCK_TOWN_DAY_2", False),
    ("Clock Town Day 3", "NA_BGM_CLOCK_TOWN_DAY_3", False),
    ("File Select", "NA_BGM_FILE_SELECT", False),
    ("Clear Event", "NA_BGM_CLEAR_EVENT", False),
    ("Enemy", "NA_BGM_ENEMY", False),
    ("Boss", "NA_BGM_BOSS", False),
    ("Woodfall Temple", "NA_BGM_WOODFALL_TEMPLE", False),
    ("Opening", "NA_BGM_OPENING", False),
    ("Inside House", "NA_BGM_INSIDE_A_HOUSE", False),
    ("Clear Boss", "NA_BGM_CLEAR_BOSS", False),
    ("Get Item", "NA_BGM_GET_ITEM", True),
    ("Get Heart", "NA_BGM_GET_HEART", True),
    ("Timed Minigame", "NA_BGM_TIMED_MINI_GAME", False),
    ("Goron Race", "NA_BGM_GORON_RACE", False),
    ("Music Box House", "NA_BGM_MUSIC_BOX_HOUSE", False),
    ("Fairy Fountain", "NA_BGM_FAIRY_FOUNTAIN", False),
    ("Zelda's Lullaby", "NA_BGM_ZELDAS_LULLABY", False),
    ("Rosa Sisters", "NA_BGM_ROSA_SISTERS", False),
    ("Open Chest", "NA_BGM_OPEN_CHEST", False),
    ("Marine Research Lab", "NA_BGM_MARINE_RESEARCH_LAB", False),
    ("Giants Theme", "NA_BGM_GIANTS_THEME", False),
    ("Song Of Storms", "NA_BGM_SONG_OF_STORMS", False),
    ("Romani Ranch", "NA_BGM_ROMANI_RANCH", False),
    ("Goron Village", "NA_BGM_GORON_VILLAGE", False),
    ("Mayors Office", "NA_BGM_MAYORS_OFFICE", False),
    ("Epona's Song", "NA_BGM_OCARINA_EPONA", True),
    ("Sun's Song", "NA_BGM_OCARINA_SUNS", True),
    ("Song Of Time", "NA_BGM_OCARINA_TIME", True),
    ("Song of Storms (Ocarina)", "NA_BGM_OCARINA_STORM", True),
    ("Zora Hall", "NA_BGM_ZORA_HALL", False),
    ("Get Mask", "NA_BGM_GET_NEW_MASK", True),
    ("Mini Boss", "NA_BGM_MINI_BOSS", False),
    ("Get Small Item", "NA_BGM_GET_SMALL_ITEM", True),
    ("Astral Observatory", "NA_BGM_ASTRAL_OBSERVATORY", False),
    ("Cavern", "NA_BGM_CAVERN", False),
    ("Milk Bar Day", "NA_BGM_MILK_BAR", False),
    ("Zelda Appear", "NA_BGM_ZELDA_APPEAR", False),
    ("Saria's Song", "NA_BGM_SARIAS_SONG", False),
    ("Goron Race Goal", "NA_BGM_GORON_GOAL", True),
    ("Horse Race", "NA_BGM_HORSE", False),
    ("Horse Race Goal", "NA_BGM_HORSE_GOAL", True),
    ("Ingo", "NA_BGM_INGO", False),
    ("Potion Shop (Kotake)", "NA_BGM_KOTAKE_POTION_SHOP", False),
    ("Shop", "NA_BGM_SHOP", False),
    ("Owl", "NA_BGM_OWL", False),
    ("Shooting Gallery", "NA_BGM_SHOOTING_GALLERY", False),
    ("Song Of Soaring", "NA_BGM_OCARINA_SOARING", True),
    ("Song Of Healing", "NA_BGM_OCARINA_HEALING", True),
    ("Inverted Song Of Time", "NA_BGM_INVERTED_SONG_OF_TIME", True),
    ("Song Of Double Time", "NA_BGM_SONG_OF_DOUBLE_TIME", True),
    ("Sonata Of Awakening", "NA_BGM_SONATA_OF_AWAKENING", True),
    ("Goron Lullaby", "NA_BGM_GORON_LULLABY", True),
    ("New Wave Bossa Nova", "NA_BGM_NEW_WAVE_BOSSA_NOVA", True),
    ("Elegy Of Emptiness", "NA_BGM_ELEGY_OF_EMPTINESS", True),
    ("Oath To Order", "NA_BGM_OATH_TO_ORDER", True),
    ("Sword Training", "NA_BGM_SWORD_TRAINING_HALL", False),
    ("Lullaby Intro", "NA_BGM_OCARINA_LULLABY_INTRO", True),
    ("Get Song", "NA_BGM_LEARNED_NEW_SONG", True),
    ("Bremen March", "NA_BGM_BREMEN_MARCH", False),
    ("Song Of Soaring", "NA_BGM_SONG_OF_SOARING", True),
    ("Milk Bar Night", "NA_BGM_MILK_BAR_DUPLICATE", False),
    ("Final Hours", "NA_BGM_FINAL_HOURS", False),
    ("Mikau Riff", "NA_BGM_MIKAU_RIFF", True),
    ("Mikau Finale", "NA_BGM_MIKAU_FINALE", True),
    ("Sonata Of Awakening (Ocarina)", "NA_BGM_OCARINA_SONATA", True),
    ("Goron Lullaby", "NA_BGM_OCARINA_LULLABY", True),
    ("New Wave Bossa Nova (Ocarina)", "NA_BGM_OCARINA_NEW_WAVE", True),
    ("Elegy Of Emptiness (Ocarina)", "NA_BGM_OCARINA_ELEGY", True),
    ("Oath To Order (Ocarina)", "NA_BGM_OCARINA_OATH", True),
    ("Majora's Lair", "NA_BGM_MAJORAS_LAIR", False),
    ("Lullaby Intro Pointer", "NA_BGM_OCARINA_LULLABY_INTRO_PTR", True),
    ("Jam Session Bass", "NA_BGM_OCARINA_GUITAR_BASS_SESSION", True),
    ("Jam Session Piano", "NA_BGM_PIANO_SESSION", True),
    ("Indigo Go Session (Credits)", "NA_BGM_INDIGO_GO_SESSION", False),
    ("Snowhead Temple", "NA_BGM_SNOWHEAD_TEMPLE", False),
    ("Great Bay Temple", "NA_BGM_GREAT_BAY_TEMPLE", False),
    ("New Wave Vocal", "NA_BGM_NEW_WAVE_VOCAL", True),
    ("Majora's Wrath", "NA_BGM_MAJORAS_WRATH", False),
    ("Majora's Incarnation", "NA_BGM_MAJORAS_INCARNATION", False),
    ("Majora's Mask", "NA_BGM_MAJORAS_MASK", False),
    ("Bass Play", "NA_BGM_BASS_PLAY", True),
    ("Drums Play", "NA_BGM_DRUMS_PLAY", True),
    ("Piano Play", "NA_BGM_PIANO_PLAY", True),
    ("Ikana Castle", "NA_BGM_IKANA_CASTLE", False),
    ("Gathering Giants", "NA_BGM_GATHERING_GIANTS", False),
    ("Kamaro Dance", "NA_BGM_KAMARO_DANCE", False),
    ("Cremia Carriage", "NA_BGM_CREMIA_CARRIAGE", False),
    ("Keaton Quiz", "NA_BGM_KEATON_QUIZ", False),
    ("Opening Loop", "NA_BGM_OPENING_LOOP", False),
    ("Title Theme", "NA_BGM_TITLE_THEME", False),
    ("Dungeon Appear", "NA_BGM_DUNGEON_APPEAR", False),
    ("Woodfall Clear", "NA_BGM_WOODFALL_CLEAR", False),
    ("Snowhead Clear", "NA_BGM_SNOWHEAD_CLEAR", False),
    ("Enter Moon", "NA_BGM_INTO_THE_MOON", False),
    ("Giants Leave", "NA_BGM_GOODBYE_GIANT", False),
    ("Tatl & Tale", "NA_BGM_TATL_AND_TAEL", False),
    ("Moon's Destruction", "NA_BGM_MOONS_DESTRUCTION", False),
]

AUDIO_EXTENSIONS = (".ogg", ".mp3", ".wav", ".flac", ".opus", ".m4a")
MAX_SAMPLE_RATE = 48000


def normalize(name):
    return "".join(c for c in name.lower() if c.isalnum())


TRACKS_BY_NAME = {}
for _label, _key, _once in TRACKS:
    TRACKS_BY_NAME.setdefault(normalize(_label), (_label, _once))
    TRACKS_BY_NAME.setdefault(normalize(_key), (_label, _once))


def _crc64_table():
    table = []
    for i in range(256):
        crc = i << 56
        for _ in range(8):
            crc = ((crc << 1) ^ 0x42F0E1EBA9EA3693) if crc & (1 << 63) else (crc << 1)
        table.append(crc & 0xFFFFFFFFFFFFFFFF)
    return table


CRC64_TABLE = _crc64_table()


def crc64(text):
    """Same hash libultraship uses for archive paths (ship/utils/StrHash64.cpp)."""
    crc = 0xFFFFFFFFFFFFFFFF
    for b in text.encode("utf-8"):
        crc = CRC64_TABLE[((crc >> 56) ^ b) & 0xFF] ^ ((crc << 8) & 0xFFFFFFFFFFFFFFFF)
    return crc


def xml_attr(value):
    return str(value).replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


def probe(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
         "stream=channels,sample_rate,duration_ts,time_base:format_tags:stream_tags", "-of", "json", path],
        check=True, capture_output=True, text=True).stdout
    data = json.loads(out)
    stream = data["streams"][0]
    tags = {}
    for source in (data.get("format", {}).get("tags", {}), stream.get("tags", {})):
        tags.update({k.upper(): v for k, v in source.items()})
    return stream, tags


def encode_channel(src, dst, channel, rate, quality):
    pan = f"pan=mono|c0=c{channel}" if channel is not None else "aformat=channel_layouts=mono"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-vn", "-af", pan, "-ar", str(rate),
                    "-c:a", "libvorbis", "-q:a", str(quality), dst], check=True)
    stream, _ = probe(dst)
    return int(stream["duration_ts"])


def read_loops(path):
    loops = {}
    if not path:
        return loops
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if "=" not in line:
                sys.exit(f"Linha inválida em {path}: {line}")
            name, value = line.rsplit("=", 1)
            parts = [p.strip() for p in value.split("-")]
            start = float(parts[0])
            end = float(parts[1]) if len(parts) > 1 and parts[1] else None
            loops[normalize(name)] = (start, end)
    return loops


def sample_xml(data_path, frames, loop_start, loop_end, once):
    return (f'<Sample Version="0" Codec="S16" Medium="Cart" bit26="0" CustomFormat="ogg" Size="{frames * 2}" '
            f'Path="{xml_attr(data_path)}">\n'
            f'<ADPCMLoop Start="{loop_start}" End="{loop_end}" Count="{0 if once else -1}"/>\n</Sample>\n')


def font_xml(sample_paths, rate, stereo):
    tuning = rate / 32000.0
    instruments = "".join(
        '<Instrument IsValid="true" Loaded="false" NormalRangeLo="0" NormalRangeHi="127" ReleaseRate="239">\n'
        '<Envelopes>\n<Envelope Delay="1" Arg="32000"/>\n<Envelope Delay="1000" Arg="32000"/>\n'
        '<Envelope Delay="-1" Arg="0"/>\n<Envelope Delay="0" Arg="0"/>\n</Envelopes>\n'
        f'<NormalNotesSound SampleRef="{xml_attr(p)}" Tuning="{tuning}"/>\n</Instrument>\n' for p in sample_paths)
    return ('<SoundFont Version="0" Num="0" Medium="Cart" CachePolicy="Either" Data1="511" Data2="3840" Data3="0">\n'
            f'<Drums/>\n<Instruments>\n{instruments}</Instruments>\n</SoundFont>\n')


def seq_xml(font_path, length, once, stereo):
    crc = crc64(font_path).to_bytes(8, "little")
    fonts = "".join(f'<FontIndex FontIdx="{b}"/>\n' for b in crc)
    return (f'<Sequence Index="0" Medium="Cart" CachePolicy="Temporary" Size="0" Streamed="true" '
            f'Length="{length}" Looped="{"false" if once else "true"}" Stereo="{"true" if stereo else "false"}">\n'
            f'<FontIndicies>\n{fonts}</FontIndicies>\n</Sequence>\n')


def main():
    parser = argparse.ArgumentParser(description="Gera um mod .o2r de músicas para o 2Ship (MM3D).")
    parser.add_argument("pasta", nargs="?", help="pasta com as músicas")
    parser.add_argument("-o", "--saida", help="arquivo .o2r gerado (padrão: <pasta>.o2r)")
    parser.add_argument("--loops", help="arquivo com os pontos de loop (padrão: loops.txt dentro da pasta)")
    parser.add_argument("--qualidade", type=int, default=6, help="qualidade Vorbis de 0 a 10 (padrão 6)")
    parser.add_argument("--lista", action="store_true", help="mostra os nomes de música aceitos e sai")
    args = parser.parse_args()

    if args.lista:
        for label, key, once in TRACKS:
            print(f"{label}{'  (toca uma vez)' if once else ''}    [{key}]")
        return
    if not args.pasta:
        parser.error("informe a pasta das músicas (ou use --lista)")
    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            sys.exit(f"Não achei o {tool}. Instale o ffmpeg e deixe-o no PATH.")

    folder = os.path.abspath(args.pasta)
    output = os.path.abspath(args.saida or folder.rstrip("/\\") + ".o2r")
    loops_path = args.loops or os.path.join(folder, "loops.txt")
    loops = read_loops(loops_path if os.path.exists(loops_path) else None)
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(AUDIO_EXTENSIONS))
    if not files:
        sys.exit(f"Nenhuma música encontrada em {folder}")

    replaced, extra = 0, []
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(output + ".tmp", "w", zipfile.ZIP_STORED) as zf:
        for index, file_name in enumerate(files):
            src = os.path.join(folder, file_name)
            name = os.path.splitext(file_name)[0].strip()
            once = False
            if name.lower().endswith("_fanfare"):
                name, once = name[: -len("_fanfare")].strip(), True
            match = TRACKS_BY_NAME.get(normalize(name))
            if match:
                once = once or match[1]
                replaced += 1
            else:
                extra.append(name)
            safe = re.sub(r"[\\/:*?\"<>|_]", " ", name).strip()
            print(f"[{index + 1}/{len(files)}] {name}" + (f" -> {match[0]}" if match else " (só no Editor de Áudio)"))

            stream, tags = probe(src)
            channels = min(int(stream["channels"]), 2)
            rate = min(int(stream["sample_rate"]), MAX_SAMPLE_RATE)
            channel_names = [f"{safe} L", f"{safe} R"] if channels == 2 else [safe]

            frames = None
            sample_refs = []
            for ch, ch_name in enumerate(channel_names):
                ogg = os.path.join(tmp, f"{index}_{ch}.ogg")
                n = encode_channel(src, ogg, ch if channels == 2 else None, rate, args.qualidade)
                frames = n if frames is None else min(frames, n)
                data_path = f"custom/sampleData/{ch_name}"
                zf.write(ogg, data_path)
                sample_refs.append((f"custom/samples/{ch_name}_SAMPLE.xml", data_path))

            loop_start, loop_end = 0, frames
            if normalize(name) in loops:
                start, end = loops[normalize(name)]
                loop_start = int(start * rate)
                loop_end = int(end * rate) if end else frames
            elif "LOOPSTART" in tags:
                src_rate = int(stream["sample_rate"])
                loop_start = int(int(tags["LOOPSTART"]) * rate / src_rate)
                if "LOOPEND" in tags:
                    loop_end = int(int(tags["LOOPEND"]) * rate / src_rate)
                elif "LOOPLENGTH" in tags:
                    loop_end = loop_start + int(int(tags["LOOPLENGTH"]) * rate / src_rate)
            loop_end = min(max(loop_end, 1), frames)
            loop_start = loop_start if 0 <= loop_start < loop_end else 0

            for xml_path, data_path in sample_refs:
                zf.writestr(xml_path, sample_xml(data_path, frames, loop_start, loop_end, once))
            font_path = f"custom/fonts/{safe}_FONT.xml"
            zf.writestr(font_path, font_xml([p for p, _ in sample_refs], rate, channels == 2))
            seq_path = f"custom/music/{safe}_SEQ.xml" + ("_fanfare" if once else "")
            zf.writestr(seq_path, seq_xml(font_path, math.ceil(frames / rate), once, channels == 2))
    os.replace(output + ".tmp", output)

    print(f"\nPronto: {output}")
    print(f"{replaced} música(s) do jogo substituída(s) automaticamente.")
    if extra:
        print("Nomes que não batem com nenhuma música do jogo (escolha no Editor de Áudio): " + ", ".join(extra))
    print('Copie o .o2r para a pasta "mods" do jogo.')


if __name__ == "__main__":
    main()
