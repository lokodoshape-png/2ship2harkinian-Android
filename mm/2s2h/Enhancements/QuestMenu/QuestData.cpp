#include "QuestData.h"

#include <cstring>

#include "2s2h/Enhancements/MM3D/BombersNotebookGuide.h"

extern "C" {
#include <z64.h>
#include <macros.h>
#include "z64bombers_notebook.h"
#include "variables.h"
extern TexturePtr gBombersNotebookPhotos[24];
s16 Inventory_GetSkullTokenCount(s16 sceneIndex);
}

namespace QuestMenu {

namespace {

// ----- Checks against the save. All of them read flags or items the game itself keeps.

bool Has(s32 item) {
    return INV_CONTENT(item) == item;
}

bool Quest_(s32 questItem) {
    return CHECK_QUEST_ITEM(questItem);
}

bool Notebook(s32 flag) {
    return CHECK_WEEKEVENTREG(flag);
}

bool Owl(s32 owl) {
    return GET_OWL_STATUE_ACTIVATED(owl) != 0;
}

int MaskCount() {
    int count = 0;
    for (s32 mask = ITEM_MASK_DEKU; mask <= ITEM_MASK_GIANT; mask++) {
        count += Has(mask) ? 1 : 0;
    }
    return count;
}

int BottleCount() {
    int count = 0;
    for (s32 slot = SLOT_BOTTLE_1; slot <= SLOT_BOTTLE_6; slot++) {
        count += (gSaveContext.save.saveInfo.inventory.items[slot] != ITEM_NONE) ? 1 : 0;
    }
    return count;
}

const char* ItemIcon(s32 item) {
    return (const char*)gItemIcons[item];
}

const char* PersonPhoto(s32 person) {
    // The photo list starts with Anju; the Bombers' photo comes after the other 19 people.
    return (const char*)gBombersNotebookPhotos[person == BOMBERS_NOTEBOOK_PERSON_BOMBERS ? 19 : person - 1];
}

std::function<bool()> HasItem(s32 item) {
    return [item] { return Has(item); };
}

std::function<bool()> HasSong(s32 song) {
    return [song] { return Quest_(song); };
}

std::function<bool()> NotebookEvent(s32 flag) {
    return [flag] { return Notebook(flag); };
}

std::function<bool()> Met(s32 person) {
    return [person] { return BombersGuide_HasMet(person); };
}

#define NB(name) WEEKEVENTREG_BOMBERS_NOTEBOOK_EVENT_##name

// ----- Main quests: the story, from the ocarina to the four Giants.

std::vector<Quest> MainQuests() {
    std::vector<Quest> q;
    q.push_back({ "main.ocarina", "A Ocarina do Tempo",
                  "Preso na forma Deku, Link tem três dias para recuperar a ocarina do Skull Kid no alto da Torre do "
                  "Relógio e voltar ao primeiro dia.",
                  "Clock Town", "Dia 3, meia-noite (topo da torre)", "Canção do Tempo e Máscara Deku",
                  ItemIcon(ITEM_OCARINA_OF_TIME), nullptr, {}, "",
                  { { "Receber a magia da Grande Fada de Clock Town", [] {
                         return gSaveContext.save.saveInfo.playerData.isMagicAcquired != 0;
                     } },
                    { "Recuperar a Ocarina do Tempo", HasItem(ITEM_OCARINA_OF_TIME) },
                    { "Aprender a Canção do Tempo", HasSong(QUEST_SONG_TIME) },
                    { "Aprender a Canção da Cura", HasSong(QUEST_SONG_HEALING) },
                    { "Voltar a ser humano e receber a Máscara Deku", HasItem(ITEM_MASK_DEKU) } } });

    Quest woodfall = { "main.woodfall", "O Pântano Envenenado",
                       "O pântano do sul foi envenenado e a princesa Deku desapareceu. Entre no Woodfall Temple e "
                       "liberte o primeiro Gigante.",
                       "Southern Swamp / Woodfall", "", "Restos de Odolwa",
                       ItemIcon(ITEM_REMAINS_ODOLWA), nullptr, HasItem(ITEM_OCARINA_OF_TIME),
                       "Recupere a Ocarina do Tempo primeiro.",
                       { { "Aprender a Sonata do Despertar no Deku Palace", HasSong(QUEST_SONG_SONATA) },
                         { "Encontrar o Arco do Herói no templo", HasItem(ITEM_BOW) },
                         { "Derrotar Odolwa", HasSong(QUEST_REMAINS_ODOLWA) } } };
    woodfall.complete = HasSong(QUEST_REMAINS_ODOLWA);
    q.push_back(woodfall);

    Quest snowhead = { "main.snowhead", "O Inverno sem Fim",
                       "Uma nevasca eterna congela as montanhas e os Gorons. Abra caminho até o Snowhead Temple.",
                       "Mountain Village / Snowhead", "", "Restos de Goht",
                       ItemIcon(ITEM_REMAINS_GOHT), nullptr, HasItem(ITEM_OCARINA_OF_TIME),
                       "Recupere a Ocarina do Tempo primeiro.",
                       { { "Receber a Máscara Goron", HasItem(ITEM_MASK_GORON) },
                         { "Aprender a Canção de Ninar Goron", HasSong(QUEST_SONG_LULLABY) },
                         { "Encontrar as Flechas de Fogo no templo", HasItem(ITEM_ARROW_FIRE) },
                         { "Derrotar Goht", HasSong(QUEST_REMAINS_GOHT) } } };
    snowhead.complete = HasSong(QUEST_REMAINS_GOHT);
    q.push_back(snowhead);

    Quest greatBay = { "main.greatbay", "O Mar Turvo",
                       "As águas da Great Bay estão turvas e os ovos da Lulu foram roubados. Leve os Zoras até o Great "
                       "Bay Temple.",
                       "Great Bay / Zora Cape", "", "Restos de Gyorg",
                       ItemIcon(ITEM_REMAINS_GYORG), nullptr, HasItem(ITEM_OCARINA_OF_TIME),
                       "Recupere a Ocarina do Tempo primeiro.",
                       { { "Receber a Máscara Zora", HasItem(ITEM_MASK_ZORA) },
                         { "Pegar o Hookshot na Pirates' Fortress", HasItem(ITEM_HOOKSHOT) },
                         { "Aprender a Nova Onda Bossa Nova", HasSong(QUEST_SONG_BOSSA_NOVA) },
                         { "Encontrar as Flechas de Gelo no templo", HasItem(ITEM_ARROW_ICE) },
                         { "Derrotar Gyorg", HasSong(QUEST_REMAINS_GYORG) } } };
    greatBay.complete = HasSong(QUEST_REMAINS_GYORG);
    q.push_back(greatBay);

    Quest ikana = { "main.ikana", "A Maldição de Ikana",
                    "Os mortos de Ikana não descansam. Suba a Stone Tower e liberte o último Gigante.",
                    "Ikana Canyon / Stone Tower", "", "Restos de Twinmold",
                    ItemIcon(ITEM_REMAINS_TWINMOLD), nullptr, HasItem(ITEM_OCARINA_OF_TIME),
                    "Recupere a Ocarina do Tempo primeiro.",
                    { { "Ver o caminho com a Lente da Verdade", HasItem(ITEM_LENS_OF_TRUTH) },
                      { "Receber a Máscara Gibdo", HasItem(ITEM_MASK_GIBDO) },
                      { "Aprender a Elegia do Vazio", HasSong(QUEST_SONG_ELEGY) },
                      { "Encontrar as Flechas de Luz no templo", HasItem(ITEM_ARROW_LIGHT) },
                      { "Derrotar Twinmold", HasSong(QUEST_REMAINS_TWINMOLD) } } };
    ikana.complete = HasSong(QUEST_REMAINS_TWINMOLD);
    q.push_back(ikana);

    Quest moon = { "main.moon", "O Chamado dos Gigantes",
                   "Com os quatro Gigantes livres, suba a Torre do Relógio na última noite e toque o Juramento à Ordem "
                   "para segurar a Lua.",
                   "Topo da Torre do Relógio", "Dia 3, meia-noite", "",
                   nullptr, "cat_main", {}, "",
                   { { "Aprender o Juramento à Ordem", HasSong(QUEST_SONG_OATH) },
                     { "Restos de Odolwa", HasSong(QUEST_REMAINS_ODOLWA) },
                     { "Restos de Goht", HasSong(QUEST_REMAINS_GOHT) },
                     { "Restos de Gyorg", HasSong(QUEST_REMAINS_GYORG) },
                     { "Restos de Twinmold", HasSong(QUEST_REMAINS_TWINMOLD) },
                     { "Na Lua, enfrentar a Majora's Mask", {} } } };
    // The game keeps no flag for beating Majora, so this one stays "in progress" at most.
    moon.complete = [] { return false; };
    q.push_back(moon);
    return q;
}

// ----- What each person's story is about and where to find them.

struct PersonInfo {
    const char* summary;
    const char* place;
};

const PersonInfo kPeopleInfo[BOMBERS_NOTEBOOK_PERSON_MAX] = {
    { "A gangue dos Bombers procura novos membros. Vença o esconde-esconde deles e ganhe o Caderno.",
      "North Clock Town" },
    { "Ajude a Anju a reencontrar o Kafei antes que o tempo acabe. Descubra onde ele está, entregue o que ele pede "
      "e una os dois no último dia.",
      "Stock Pot Inn (East Clock Town)" },
    { "O noivo da Anju sumiu depois de virar criança. Ele se esconde enquanto tenta recuperar a Máscara do Sol.",
      "Laundry Pool (South Clock Town)" },
    { "O dono da Curiosity Shop guarda o segredo do Kafei e compra o que o Sakon rouba.", "West Clock Town" },
    { "A dona da Bomb Shop volta tarde para casa no primeiro dia, e o ladrão Sakon está de olho nela.",
      "North Clock Town" },
    { "A Romani diz que todo ano, na véspera do festival, 'eles' vêm levar as vacas do rancho.", "Romani Ranch" },
    { "A Cremia leva o leite até Clock Town, mas a estrada tem bandidos.", "Romani Ranch" },
    { "O prefeito está preso numa reunião sobre o festival e não sabe onde o filho foi parar.",
      "Mayor's Residence (East Clock Town)" },
    { "A mãe do Kafei quer notícias dele e pede ajuda para encontrá-lo.", "Mayor's Residence / Milk Bar" },
    { "O empresário da trupe de Gorman se lamenta no Milk Bar.", "Milk Bar (East Clock Town)" },
    { "O líder da trupe de Gorman não aguenta mais o fracasso e só se anima com um bom show.",
      "Milk Bar (East Clock Town)" },
    { "O carteiro leva o trabalho a sério demais. Só um pedido muito importante o faz largar o expediente.",
      "Post Office (West Clock Town)" },
    { "As irmãs Rosa querem uma dança nova para o festival.", "West Clock Town" },
    { "Uma mão misteriosa sai da privada da pousada pedindo papel.", "Stock Pot Inn" },
    { "A avó da Anju adora contar histórias, mas é difícil ficar acordado até o fim.", "Stock Pot Inn" },
    { "O espírito de um dançarino ainda dança no campo à noite.", "Termina Field" },
    { "O Grog quer ver os pintinhos crescerem antes do fim do mundo.", "Cucco Shack (Romani Ranch)" },
    { "Os irmãos Gorman apostam corridas de cavalo e tramam contra o rancho.", "Gorman Track (Milk Road)" },
    { "Um soldado invisível espera ajuda na estrada para Ikana.", "Road to Ikana" },
    { "Um músico triste toca na Laundry Pool e tem algo a confessar.", "Laundry Pool (South Clock Town)" },
};

std::string DaysText(u8 days) {
    if (days == ((1 << 1) | (1 << 2) | (1 << 3))) {
        return "Qualquer dia";
    }
    std::string out;
    for (int d = 1; d <= 3; d++) {
        if (days & (1 << d)) {
            out += (out.empty() ? "Dia " : ", Dia ") + std::to_string(d);
        }
    }
    return out;
}

// A step's text without its "Recompensa: ..." tail, and that tail.
std::string StepText(const char* task) {
    std::string text = task;
    size_t r = text.find(" Recompensa: ");
    if (r == std::string::npos) {
        r = text.find(". Recompensa: ");
    }
    return r == std::string::npos ? text : text.substr(0, r);
}

std::string StepReward(const char* task) {
    const char* r = strstr(task, "Recompensa: ");
    return r == nullptr ? "" : std::string(r + strlen("Recompensa: "));
}

// ----- Side quests: each person's Bombers' Notebook story, step by step.

std::vector<Quest> SideQuests() {
    std::vector<Quest> q;
    for (s32 person = 0; person < BOMBERS_NOTEBOOK_PERSON_MAX; person++) {
        const GuidePerson& guide = BombersGuide_Person(person);
        Quest quest;
        quest.id = "side." + std::to_string(person);
        quest.name = guide.name;
        quest.description = kPeopleInfo[person].summary;
        quest.region = kPeopleInfo[person].place;
        quest.icon = PersonPhoto(person);
        quest.unlocked = Met(person);
        quest.lockedHint = "Encontre esta pessoa para o Caderno dos Bombers registrar a história.";
        u8 days = 0;
        std::string rewards;
        for (const GuideStep& step : guide.steps) {
            days |= step.days;
            std::string text = DaysText(step.days) + ", " + step.time + ": " + StepText(step.task);
            GuideStep copy = step;
            quest.objectives.push_back(
                { text, step.doneFlag != 0 ? std::function<bool()>([copy] { return BombersGuide_IsStepDone(copy); })
                                           : std::function<bool()>() });
            std::string reward = StepReward(step.task);
            if (!reward.empty() && rewards.find(reward) == std::string::npos) {
                rewards += (rewards.empty() ? "" : ", ") + reward;
            }
        }
        quest.when = DaysText(days);
        quest.reward = rewards;
        q.push_back(quest);
    }
    return q;
}

// ----- People: who Link has met, and how far their story in the notebook is.

std::vector<Quest> People() {
    std::vector<Quest> q;
    for (s32 person = 0; person < BOMBERS_NOTEBOOK_PERSON_MAX; person++) {
        const GuidePerson& guide = BombersGuide_Person(person);
        Quest quest;
        quest.id = "person." + std::to_string(person);
        quest.name = guide.name;
        quest.description = kPeopleInfo[person].summary;
        quest.region = kPeopleInfo[person].place;
        quest.icon = PersonPhoto(person);
        quest.objectives.push_back({ "Conhecer e ser registrado no Caderno", Met(person) });
        std::vector<GuideStep> steps = guide.steps;
        quest.objectives.push_back({ "Completar a história no Caderno", [steps] {
                                        for (const GuideStep& step : steps) {
                                            if (step.doneFlag != 0 && !BombersGuide_IsStepDone(step)) {
                                                return false;
                                            }
                                        }
                                        return !steps.empty();
                                    } });
        q.push_back(quest);
    }
    return q;
}

// ----- Masks: all 24, each with the steps to get it.

Quest Mask(s32 item, const char* name, const char* region, const char* when, const char* description,
           std::vector<Objective> steps) {
    Quest quest;
    quest.id = std::string("mask.") + std::to_string(item);
    quest.name = name;
    quest.region = region;
    quest.when = when;
    quest.description = description;
    quest.reward = name;
    quest.icon = ItemIcon(item);
    steps.push_back({ std::string("Receber: ") + name, HasItem(item) });
    quest.objectives = steps;
    quest.complete = HasItem(item);
    return quest;
}

std::vector<Quest> Masks() {
    std::vector<Quest> q;
    auto healing = HasSong(QUEST_SONG_HEALING);
    q.push_back(Mask(ITEM_MASK_DEKU, "Máscara Deku", "Torre do Relógio (Clock Town)", "Dia 1 a Dia 3",
                     "O Vendedor de Máscaras desfaz a maldição Deku quando você volta com a ocarina.",
                     { { "Recuperar a Ocarina do Tempo", HasItem(ITEM_OCARINA_OF_TIME) },
                       { "Aprender a Canção da Cura com o Vendedor de Máscaras", healing } }));
    Quest goron = Mask(ITEM_MASK_GORON, "Máscara Goron", "Cemitério Goron (Mountain Village)", "Qualquer dia",
                       "O fantasma do herói Darmani não descansa. Siga-o com a Lente da Verdade e cure sua alma.",
                       { { "Conseguir a Lente da Verdade", HasItem(ITEM_LENS_OF_TRUTH) },
                         { "Seguir o fantasma do Darmani até o túmulo", {} },
                         { "Tocar a Canção da Cura para ele", {} } });
    goron.unlocked = healing;
    goron.lockedHint = "Aprenda a Canção da Cura primeiro.";
    q.push_back(goron);
    Quest zora = Mask(ITEM_MASK_ZORA, "Máscara Zora", "Great Bay Coast", "Qualquer dia",
                      "O guitarrista Mikau chega ferido à praia. Leve-o à areia e toque a Canção da Cura.",
                      { { "Empurrar o Mikau até a praia", {} }, { "Tocar a Canção da Cura para ele", {} } });
    zora.unlocked = healing;
    zora.lockedHint = "Aprenda a Canção da Cura primeiro.";
    q.push_back(zora);
    Quest fierce = Mask(ITEM_MASK_FIERCE_DEITY, "Máscara da Divindade Feroz", "A Lua", "Fim do Dia 3",
                        "Na Lua, as crianças com máscaras pedem as máscaras de Termina. Entregue todas.",
                        { { "Ter as outras 23 máscaras", [] { return MaskCount() >= 23; } },
                          { "Entregar as máscaras às crianças na Lua", {} } });
    fierce.counter = { [] { return MaskCount(); }, 24 };
    q.push_back(fierce);
    Quest truth = Mask(ITEM_MASK_TRUTH, "Máscara da Verdade", "Swamp Spider House (Southern Swamp)", "Qualquer dia",
                       "A família amaldiçoada da casa das aranhas do pântano volta ao normal quando todas as Gold "
                       "Skulltulas são destruídas.",
                       { { "Destruir as 30 Gold Skulltulas da Swamp Spider House",
                           [] { return Inventory_GetSkullTokenCount(SCENE_KINSTA1) >= 30; } } });
    truth.counter = { [] { return (int)Inventory_GetSkullTokenCount(SCENE_KINSTA1); }, 30 };
    q.push_back(truth);
    q.push_back(Mask(ITEM_MASK_KAFEIS_MASK, "Máscara do Kafei", "Mayor's Residence (East Clock Town)",
                     "Dia 1 e Dia 2, de dia", "A Madame Aroma pede ajuda para encontrar o filho e entrega a máscara dele.",
                     { { "Conhecer a Madame Aroma", Met(BOMBERS_NOTEBOOK_PERSON_MADAME_AROMA) },
                       { "Aceitar procurar o Kafei", NotebookEvent(NB(RECEIVED_KAFEIS_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_ALL_NIGHT, "Máscara da Noite Toda", "Curiosity Shop (West Clock Town)",
                     "Dia 3, 22:00 às 6:00",
                     "Se o Sakon não roubar a bolsa de bombas, a máscara aparece à venda na Curiosity Shop.",
                     { { "Salvar a dona da Bomb Shop do Sakon (Dia 1, meia-noite)",
                         NotebookEvent(NB(RECEIVED_BLAST_MASK)) },
                       { "Comprar por 500 rúpias na Curiosity Shop", NotebookEvent(NB(RECEIVED_ALL_NIGHT_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_BUNNY, "Capuz de Coelho", "Cucco Shack (Romani Ranch)", "Qualquer dia, de dia",
                     "O Grog quer ver os pintinhos crescerem antes do fim do mundo.",
                     { { "Ter a Máscara Bremen", HasItem(ITEM_MASK_BREMEN) },
                       { "Marchar com os 10 pintinhos até virarem galinhas", NotebookEvent(NB(RECEIVED_BUNNY_HOOD)) } }));
    q.push_back(Mask(ITEM_MASK_KEATON, "Máscara Keaton", "Laundry Pool (South Clock Town)", "Dia 3, 13:00 às 22:00",
                     "Na sala dos fundos da Curiosity Shop, o Kafei espera o Sakon. Depois que ele sai, o dono da loja "
                     "entrega a máscara.",
                     { { "Encontrar o Kafei na sala dos fundos", Met(BOMBERS_NOTEBOOK_PERSON_KAFEI) },
                       { "Receber a máscara do dono da loja", NotebookEvent(NB(RECEIVED_KEATON_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_GARO, "Máscara do Garo", "Gorman Track (Milk Road)", "Dia 1 e Dia 2, de dia",
                     "Os irmãos Gorman desafiam qualquer um para uma corrida de cavalo.",
                     { { "Ter a Canção da Epona", HasSong(QUEST_SONG_EPONA) },
                       { "Vencer a corrida dos irmãos Gorman", NotebookEvent(NB(RECEIVED_GAROS_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_ROMANI, "Máscara da Romani", "Romani Ranch", "Dia 2, 18:00",
                     "Proteja a carroça de leite da Cremia dos bandidos no caminho para Clock Town.",
                     { { "Salvar as vacas da Romani na noite do Dia 1", NotebookEvent(NB(RECEIVED_ALIENS_BOTTLE)) },
                       { "Escoltar a Cremia pela Gorman Track", NotebookEvent(NB(RECEIVED_ROMANIS_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_CIRCUS_LEADER, "Máscara do Líder do Circo", "Milk Bar (East Clock Town)",
                     "Dia 1 e Dia 2, 22:00 às 5:00",
                     "O Gorman fica comovido com uma música tocada com as máscaras Deku, Goron e Zora.",
                     { { "Ter a Máscara da Romani para entrar no Milk Bar", HasItem(ITEM_MASK_ROMANI) },
                       { "Ter as máscaras Deku, Goron e Zora",
                         [] { return Has(ITEM_MASK_DEKU) && Has(ITEM_MASK_GORON) && Has(ITEM_MASK_ZORA); } },
                       { "Tocar no palco para o Gorman", NotebookEvent(NB(RECEIVED_CIRCUS_LEADERS_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_POSTMAN, "Chapéu do Carteiro", "Post Office (West Clock Town)", "Dia 3, à noite",
                     "Só uma entrega muito importante faz o carteiro largar o horário.",
                     { { "Receber a Priority Mail do Kafei", NotebookEvent(NB(RECEIVED_PRIORITY_MAIL)) },
                       { "Entregar a Priority Mail ao carteiro", NotebookEvent(NB(RECEIVED_POSTMANS_HAT)) } }));
    q.push_back(Mask(ITEM_MASK_COUPLE, "Máscara do Casal", "Stock Pot Inn (East Clock Town)", "Dia 3, 4:00 às 6:00",
                     "A Anju espera o Kafei até o fim. Una os dois antes da Lua cair.",
                     { { "Receber a Room Key / falar com a Anju", NotebookEvent(NB(RECEIVED_ROOM_KEY)) },
                       { "Entregar a carta da Anju ao Kafei", NotebookEvent(NB(RECEIVED_LETTER_TO_KAFEI)) },
                       { "Receber o Pendant of Memories", NotebookEvent(NB(RECEIVED_PENDANT_OF_MEMORIES)) },
                       { "Ajudar o Kafei a recuperar a Máscara do Sol", {} },
                       { "Ver o reencontro na Stock Pot Inn", NotebookEvent(NB(RECEIVED_COUPLES_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_GREAT_FAIRY, "Máscara da Grande Fada", "Fonte das Fadas (North Clock Town)",
                     "Qualquer dia", "Devolva a fada perdida de Clock Town à fonte em forma humana.",
                     { { "Levar a fada perdida de Clock Town para a fonte", [] {
                          return CHECK_WEEKEVENTREG(WEEKEVENTREG_08_80);
                      } } }));
    Quest gibdo = Mask(ITEM_MASK_GIBDO, "Máscara Gibdo", "Music Box House (Ikana Canyon)", "Qualquer dia",
                       "O pai da Pamela virou meio Gibdo. Cure-o com a Canção da Cura.",
                       { { "Fazer a música da casa voltar a tocar", {} },
                         { "Tocar a Canção da Cura para o pai da Pamela", {} } });
    gibdo.unlocked = healing;
    gibdo.lockedHint = "Aprenda a Canção da Cura primeiro.";
    q.push_back(gibdo);
    q.push_back(Mask(ITEM_MASK_DON_GERO, "Máscara do Don Gero", "Mountain Village", "Qualquer dia",
                     "Um Goron faminto sonha com o Rock Sirloin. Leve a carne até ele antes que esfrie.",
                     { { "Ter a Máscara Goron", HasItem(ITEM_MASK_GORON) },
                       { "Levar o Rock Sirloin ao Goron faminto", {} } }));
    Quest kamaro = Mask(ITEM_MASK_KAMARO, "Máscara do Kamaro", "Termina Field (norte)", "Qualquer dia, 0:00 às 6:00",
                        "O fantasma do dançarino Kamaro quer passar sua dança adiante.",
                        { { "Tocar a Canção da Cura para o Kamaro", NotebookEvent(NB(RECEIVED_KAMAROS_MASK)) } });
    kamaro.unlocked = healing;
    kamaro.lockedHint = "Aprenda a Canção da Cura primeiro.";
    q.push_back(kamaro);
    q.push_back(Mask(ITEM_MASK_CAPTAIN, "Chapéu do Capitão", "Cemitério de Ikana", "Dia 1, à noite",
                     "O Capitão Keeta dorme sob o cemitério. Acorde-o e vença a corrida até o baú.",
                     { { "Aprender a Sonata do Despertar", HasSong(QUEST_SONG_SONATA) },
                       { "Acordar e vencer o Capitão Keeta", {} } }));
    q.push_back(Mask(ITEM_MASK_STONE, "Máscara de Pedra", "Road to Ikana", "Qualquer hora",
                     "O soldado Shiro ficou invisível de tanto ser esquecido.",
                     { { "Ver o Shiro com a Lente da Verdade", HasItem(ITEM_LENS_OF_TRUTH) },
                       { "Dar uma Poção Vermelha a ele", NotebookEvent(NB(RECEIVED_STONE_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_BREMEN, "Máscara Bremen", "Laundry Pool (South Clock Town)",
                     "Dia 1 e Dia 2, 18:00 às 6:00", "O músico Guru-Guru tem uma confissão a fazer.",
                     { { "Ouvir a história do Guru-Guru", NotebookEvent(NB(RECEIVED_BREMEN_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_BLAST, "Máscara Explosiva", "North Clock Town", "Dia 1, meia-noite",
                     "O ladrão Sakon ataca a dona da Bomb Shop quando ela volta para casa.",
                     { { "Impedir o Sakon de roubar a bolsa", NotebookEvent(NB(RECEIVED_BLAST_MASK)) } }));
    q.push_back(Mask(ITEM_MASK_SCENTS, "Máscara dos Aromas", "Deku Palace", "Qualquer dia",
                     "O Mordomo Deku desafia Link para uma corrida pelo santuário depois de salvar a princesa.",
                     { { "Libertar Woodfall (derrotar Odolwa)", HasSong(QUEST_REMAINS_ODOLWA) },
                       { "Vencer a corrida do Mordomo Deku", {} } }));
    q.push_back(Mask(ITEM_MASK_GIANT, "Máscara do Gigante", "Stone Tower Temple",
                     "Qualquer dia",
                     "Guardada num baú do templo invertido (ou recebida na luta contra o Twinmold, com a opção do MM3D).",
                     { { "Encontrar as Flechas de Luz", HasItem(ITEM_ARROW_LIGHT) },
                       { "Abrir o baú no templo invertido", {} } }));
    return q;
}

// ----- Places: the owl statues Link has woken up.

std::vector<Quest> Places() {
    struct OwlPlace {
        s32 owl;
        const char* name;
        const char* region;
        const char* description;
    };
    static const OwlPlace kOwls[] = {
        { OWL_WARP_CLOCK_TOWN, "Clock Town", "South Clock Town", "A estátua da praça, ao lado da Torre do Relógio." },
        { OWL_WARP_MILK_ROAD, "Milk Road", "Milk Road", "No caminho para o Romani Ranch." },
        { OWL_WARP_SOUTHERN_SWAMP, "Southern Swamp", "Southern Swamp", "Perto do Swamp Tourist Center." },
        { OWL_WARP_WOODFALL, "Woodfall", "Woodfall", "Diante do Woodfall Temple." },
        { OWL_WARP_MOUNTAIN_VILLAGE, "Mountain Village", "Mountain Village", "No vilarejo dos ferreiros." },
        { OWL_WARP_SNOWHEAD, "Snowhead", "Snowhead", "No caminho para o Snowhead Temple." },
        { OWL_WARP_GREAT_BAY_COAST, "Great Bay Coast", "Great Bay Coast", "Perto do Marine Research Lab." },
        { OWL_WARP_ZORA_CAPE, "Zora Cape", "Zora Cape", "No penhasco do Great Bay Temple." },
        { OWL_WARP_IKANA_CANYON, "Ikana Canyon", "Ikana Canyon", "No vale perto da Music Box House." },
        { OWL_WARP_STONE_TOWER, "Stone Tower", "Stone Tower", "No alto da torre, diante do templo." },
    };
    std::vector<Quest> q;
    for (const OwlPlace& place : kOwls) {
        Quest quest;
        quest.id = std::string("place.owl.") + std::to_string(place.owl);
        quest.name = place.name;
        quest.region = place.region;
        quest.description = std::string("Estátua da coruja. ") + place.description +
                            " Acordada com a espada, ela salva o jogo e recebe voos da Canção do Voo.";
        quest.iconAsset = "cat_places";
        s32 owl = place.owl;
        quest.objectives.push_back({ "Acordar a estátua da coruja", [owl] { return Owl(owl); } });
        q.push_back(quest);
    }
    return q;
}

// ----- Important items.

Quest Item(s32 item, const char* name, const char* region, const char* description) {
    Quest quest;
    quest.id = std::string("item.") + std::to_string(item);
    quest.name = name;
    quest.region = region;
    quest.description = description;
    quest.icon = ItemIcon(item);
    quest.objectives.push_back({ std::string("Conseguir: ") + name, HasItem(item) });
    return quest;
}

std::vector<Quest> Items() {
    std::vector<Quest> q;
    q.push_back(Item(ITEM_OCARINA_OF_TIME, "Ocarina do Tempo", "Topo da Torre do Relógio",
                     "Recuperada do Skull Kid na última noite. Volta o tempo e toca as canções."));
    q.push_back(Item(ITEM_BOW, "Arco do Herói", "Woodfall Temple", "Guardado no templo do pântano."));
    q.push_back(Item(ITEM_ARROW_FIRE, "Flechas de Fogo", "Snowhead Temple", "Derretem o gelo das montanhas."));
    q.push_back(Item(ITEM_ARROW_ICE, "Flechas de Gelo", "Great Bay Temple", "Congelam a água e os inimigos."));
    q.push_back(Item(ITEM_ARROW_LIGHT, "Flechas de Luz", "Stone Tower Temple", "Viram a Stone Tower de cabeça para baixo."));
    q.push_back(Item(ITEM_BOMB, "Bolsa de Bombas", "Bomb Shop (West Clock Town)", "Comprada na loja de bombas."));
    q.push_back(Item(ITEM_LENS_OF_TRUTH, "Lente da Verdade", "Goron Village (pico solitário)",
                     "Mostra o que está escondido."));
    q.push_back(Item(ITEM_HOOKSHOT, "Hookshot", "Pirates' Fortress", "Guardado no interior da fortaleza."));
    q.push_back(Item(ITEM_PICTOGRAPH_BOX, "Pictograph Box", "Swamp Tourist Center",
                     "Prêmio da Koume depois de salvá-la no Woods of Mystery."));
    q.push_back(Item(ITEM_POWDER_KEG, "Barril de Pólvora", "Goron Village",
                     "O Medigoron ensina a usar, em forma Goron, depois do Snowhead Temple."));
    q.push_back(Item(ITEM_MAGIC_BEANS, "Feijões Mágicos", "Deku Palace",
                     "O vendedor de feijões fica numa gruta do palácio."));
    q.push_back(Item(ITEM_SWORD_GREAT_FAIRY, "Espada da Grande Fada", "Fonte de Ikana Canyon",
                     "Prêmio das 15 fadas perdidas do Stone Tower Temple."));
    Quest bottles;
    bottles.id = "item.bottles";
    bottles.name = "Garrafas";
    bottles.region = "Termina";
    bottles.description = "Seis garrafas espalhadas por Termina guardam poções, leite, fadas e peixes.";
    bottles.icon = ItemIcon(ITEM_BOTTLE);
    bottles.counter = { [] { return BottleCount(); }, 6 };
    q.push_back(bottles);
    Quest magic;
    magic.id = "item.magic";
    magic.name = "Magia";
    magic.region = "Fontes das Grandes Fadas";
    magic.description = "A Grande Fada de Clock Town dá a magia; a de Snowhead dobra a barra.";
    magic.iconAsset = "cat_items";
    magic.objectives = { { "Receber a magia", [] { return gSaveContext.save.saveInfo.playerData.isMagicAcquired != 0; } },
                         { "Dobrar a magia", [] {
                              return gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired != 0;
                          } } };
    q.push_back(magic);
    return q;
}

// ----- Records: songs, the notebook and the collections the game counts.

std::vector<Quest> Records() {
    struct Song {
        s32 quest;
        const char* name;
        const char* region;
        const char* description;
    };
    static const Song kSongs[] = {
        { QUEST_SONG_TIME, "Canção do Tempo", "Topo da Torre do Relógio", "Recupere a ocarina do Skull Kid." },
        { QUEST_SONG_HEALING, "Canção da Cura", "Torre do Relógio", "O Vendedor de Máscaras ensina." },
        { QUEST_SONG_SOARING, "Canção do Voo", "Woodfall", "Leia a pedra perto da entrada do templo." },
        { QUEST_SONG_EPONA, "Canção da Epona", "Romani Ranch", "A Romani ensina depois do treino com o arco." },
        { QUEST_SONG_STORMS, "Canção das Tempestades", "Cemitério de Ikana", "O fantasma do Flat ensina." },
        { QUEST_SONG_SONATA, "Sonata do Despertar", "Deku Palace", "O macaco preso ensina." },
        { QUEST_SONG_LULLABY, "Canção de Ninar Goron", "Goron Shrine / Mountain Village",
          "O ancião começa, o filho dele termina." },
        { QUEST_SONG_BOSSA_NOVA, "Nova Onda Bossa Nova", "Marine Research Lab", "Devolva os ovos da Lulu." },
        { QUEST_SONG_ELEGY, "Elegia do Vazio", "Ancient Castle of Ikana", "Derrote o Igos du Ikana." },
        { QUEST_SONG_OATH, "Juramento à Ordem", "Os Gigantes", "Ensinado depois do primeiro templo." },
    };
    std::vector<Quest> q;
    for (const Song& song : kSongs) {
        Quest quest;
        quest.id = std::string("record.song.") + std::to_string(song.quest);
        quest.name = song.name;
        quest.region = song.region;
        quest.description = song.description;
        quest.icon = ItemIcon(ITEM_SONG_SONATA + (song.quest - QUEST_SONG_SONATA));
        quest.objectives.push_back({ "Aprender a canção", HasSong(song.quest) });
        q.push_back(quest);
    }

    Quest notebook;
    notebook.id = "record.notebook";
    notebook.name = "Caderno dos Bombers";
    notebook.region = "North Clock Town";
    notebook.description = "Vença o esconde-esconde dos Bombers para ganhar o caderno que registra as pessoas.";
    notebook.icon = ItemIcon(ITEM_BOMBERS_NOTEBOOK);
    notebook.objectives.push_back({ "Receber o Caderno dos Bombers", HasSong(QUEST_BOMBERS_NOTEBOOK) });
    q.push_back(notebook);

    static const char* kFairyTemples[4] = { "Woodfall Temple", "Snowhead Temple", "Great Bay Temple",
                                            "Stone Tower Temple" };
    for (int i = 0; i < 4; i++) {
        Quest fairies;
        fairies.id = "record.fairies." + std::to_string(i);
        fairies.name = std::string("Fadas: ") + kFairyTemples[i];
        fairies.region = kFairyTemples[i];
        fairies.description = "Quinze fadas perdidas estão presas no templo. Leve todas à Grande Fada.";
        fairies.icon = ItemIcon(ITEM_MASK_GREAT_FAIRY);
        fairies.counter = { [i] { return (int)gSaveContext.save.saveInfo.inventory.strayFairies[i]; }, 15 };
        q.push_back(fairies);
    }

    Quest swampSkulls;
    swampSkulls.id = "record.skulls.swamp";
    swampSkulls.name = "Skulltulas do Pântano";
    swampSkulls.region = "Swamp Spider House";
    swampSkulls.description = "30 Gold Skulltulas na casa das aranhas do pântano.";
    swampSkulls.icon = ItemIcon(ITEM_SKULL_TOKEN);
    swampSkulls.counter = { [] { return (int)Inventory_GetSkullTokenCount(SCENE_KINSTA1); }, 30 };
    q.push_back(swampSkulls);

    Quest oceanSkulls = swampSkulls;
    oceanSkulls.id = "record.skulls.ocean";
    oceanSkulls.name = "Skulltulas do Oceano";
    oceanSkulls.region = "Oceanside Spider House";
    oceanSkulls.description = "30 Gold Skulltulas na casa das aranhas do litoral.";
    oceanSkulls.counter = { [] { return (int)Inventory_GetSkullTokenCount(SCENE_KINDAN2); }, 30 };
    q.push_back(oceanSkulls);

    Quest hearts;
    hearts.id = "record.hearts";
    hearts.name = "Corações";
    hearts.region = "Termina";
    hearts.description = "Recipientes e Pedaços de Coração (quatro pedaços fazem um coração). O máximo são 20.";
    hearts.icon = ItemIcon(ITEM_HEART_CONTAINER);
    hearts.counter = { [] { return gSaveContext.save.saveInfo.playerData.healthCapacity / 0x10; }, 20 };
    q.push_back(hearts);
    return q;
}

} // namespace

const char* CategoryName(Category category) {
    switch (category) {
        case Category::Main: return "Missões principais";
        case Category::Side: return "Missões secundárias";
        case Category::Masks: return "Máscaras";
        case Category::People: return "Personagens";
        case Category::Places: return "Locais descobertos";
        case Category::Items: return "Itens importantes";
        case Category::Records: return "Registros";
        default: return "";
    }
}

const char* CategoryAsset(Category category) {
    switch (category) {
        case Category::Main: return "cat_main";
        case Category::Side: return "cat_side";
        case Category::Masks: return "cat_masks";
        case Category::People: return "cat_people";
        case Category::Places: return "cat_places";
        case Category::Items: return "cat_items";
        case Category::Records: return "cat_records";
        default: return "";
    }
}

const std::vector<Quest>& Quests(Category category) {
    static std::vector<Quest> sTables[(int)Category::Count];
    static bool sBuilt = false;
    if (!sBuilt) {
        sBuilt = true;
        sTables[(int)Category::Main] = MainQuests();
        sTables[(int)Category::Side] = SideQuests();
        sTables[(int)Category::Masks] = Masks();
        sTables[(int)Category::People] = People();
        sTables[(int)Category::Places] = Places();
        sTables[(int)Category::Items] = Items();
        sTables[(int)Category::Records] = Records();
    }
    return sTables[(int)category];
}

} // namespace QuestMenu
