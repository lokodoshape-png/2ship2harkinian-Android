#include "BombersNotebookGuide.h"
#include "MM3D.h"
#include "2s2h/BenGui/UIWidgets.hpp"

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <imgui_internal.h>
#include <spdlog/fmt/fmt.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <z64.h>
#include <macros.h>
#include "z64bombers_notebook.h"
#include "variables.h"

extern PlayState* gPlayState;
extern BombersNotebook sBombersNotebook;
extern u8 sBombersNotebookOpen;
}

#define CVAR_NAME "gEnhancements.MM3D.BombersNotebookGuide"
#define NOTEBOOK_FLAG(name) WEEKEVENTREG_BOMBERS_NOTEBOOK_EVENT_##name

// Days a step can be done on, matching the three columns of the notebook schedule
#define DAY_1 (1 << 1)
#define DAY_2 (1 << 2)
#define DAY_3 (1 << 3)
#define ANY_DAY (DAY_1 | DAY_2 | DAY_3)

typedef struct {
    u8 days;
    const char* time;
    const char* task;
    u16 doneFlag; // Notebook flag set once this step is recorded, 0 when there is none
} GuideStep;

typedef struct {
    const char* name;
    std::vector<GuideStep> steps;
} GuidePerson;

// Indexed by BombersNotebookPerson, in the same order as the notebook rows
static const GuidePerson sGuide[BOMBERS_NOTEBOOK_PERSON_MAX] = {
    { "Bombers (Jim)",
      { { ANY_DAY, "Durante o dia", "North Clock Town: em forma humana, estoure o balão que o Jim tenta pegar e ache os 5 "
                                    "Bombers. Recompensa: Bombers' Notebook",
          NOTEBOOK_FLAG(RECEIVED_BOMBERS_NOTEBOOK) },
        { ANY_DAY, "Durante o dia", "Em forma Deku, repita o esconde-esconde. Recompensa: o código secreto que passa pelo "
                                    "guarda do observatório",
          NOTEBOOK_FLAG(LEARNED_SECRET_CODE) } } },
    { "Anju",
      { { DAY_1, "Antes das 14:00", "Balcão da Stock Pot Inn: diga que tem uma reserva. Recompensa: Room Key",
          NOTEBOOK_FLAG(RECEIVED_ROOM_KEY) },
        { DAY_1, "À tarde", "Fale com a Anju no balcão usando a Kafei's Mask e prometa o encontro da meia-noite",
          NOTEBOOK_FLAG(PROMISED_MIDNIGHT_MEETING) },
        { DAY_1, "23:30", "Cozinha da Stock Pot Inn: encontre a Anju. Recompensa: Letter to Kafei",
          NOTEBOOK_FLAG(RECEIVED_LETTER_TO_KAFEI) },
        { DAY_3, "Durante o dia", "Entregue o Pendant of Memories para a Anju na Stock Pot Inn",
          NOTEBOOK_FLAG(DELIVERED_PENDANT_OF_MEMORIES) },
        { DAY_3, "4:00 às 6:00 (amanhecer final)",
          "Espere no quarto da Anju depois de ajudar o Kafei no Sakon's Hideout. Recompensa: Couple's Mask",
          NOTEBOOK_FLAG(RECEIVED_COUPLES_MASK) } } },
    { "Kafei",
      { { DAY_1 | DAY_2, "Até o fim da manhã do Dia 2",
          "Coloque a Letter to Kafei em qualquer caixa de correio da cidade", NOTEBOOK_FLAG(DEPOSITED_LETTER_TO_KAFEI) },
        { DAY_2, "Depois das 15:00", "Sala dos fundos da Curiosity Shop (Laundry Pool): fale com o Kafei. Recompensa: "
                                     "Pendant of Memories",
          NOTEBOOK_FLAG(RECEIVED_PENDANT_OF_MEMORIES) },
        { DAY_3, "Por volta das 18:00",
          "Ikana Canyon, entrada do Sakon's Hideout: ajude o Kafei a recuperar a Sun's Mask",
          NOTEBOOK_FLAG(ESCAPED_SAKONS_HIDEOUT) } } },
    { "Curiosity Shop Man",
      { { DAY_3, "13:00 às 22:00", "Sala dos fundos da Curiosity Shop (Laundry Pool), depois que o Kafei sai. "
                                   "Recompensa: Keaton Mask",
          NOTEBOOK_FLAG(RECEIVED_KEATON_MASK) },
        { DAY_3, "13:00 às 22:00", "Na mesma conversa ele entrega a Priority Mail", NOTEBOOK_FLAG(RECEIVED_PRIORITY_MAIL) },
        { DAY_3, "22:00 às 6:00", "Curiosity Shop (West Clock Town): compre a All-Night Mask por 500 rúpias. Precisa ter "
                                  "salvo a Bomb Shop Lady no Dia 1 e ter a Giant's Wallet",
          NOTEBOOK_FLAG(RECEIVED_ALL_NIGHT_MASK) } } },
    { "Bomb Shop Lady",
      { { DAY_1, "0:00 (meia-noite)",
          "North Clock Town: ataque o Sakon quando ele rouba a bolsa dela. Recompensa: Blast Mask",
          NOTEBOOK_FLAG(RECEIVED_BLAST_MASK) } } },
    { "Romani",
      { { DAY_1, "Antes das 18:00", "Romani Ranch: treine com o arco e prometa ajudar contra os fantasmas",
          NOTEBOOK_FLAG(PROMISED_TO_HELP_WITH_ALIENS) },
        { DAY_1, "2:30 às 5:15", "Defenda o celeiro dos fantasmas com o arco", NOTEBOOK_FLAG(DEFENDED_AGAINST_ALIENS) },
        { DAY_2, "5:15 (amanhecer)", "A Romani agradece. Recompensa: garrafa com Milk",
          NOTEBOOK_FLAG(RECEIVED_ALIENS_BOTTLE) } } },
    { "Cremia",
      { { DAY_2, "18:00", "Celeiro do Romani Ranch: proteja a carroça da Cremia com o arco até Clock Town",
          NOTEBOOK_FLAG(ESCORTED_CREMIA) },
        { DAY_2, "Ao chegar", "Recompensa: Romani's Mask (precisa ter defendido o rancho na noite do Dia 1)",
          NOTEBOOK_FLAG(RECEIVED_ROMANIS_MASK) } } },
    { "Mayor Dotour",
      { { ANY_DAY, "10:00 às 20:00", "Gabinete do prefeito (East Clock Town): use a Couple's Mask na reunião. "
                                     "Recompensa: Piece of Heart",
          NOTEBOOK_FLAG(RECEIVED_MAYOR_HP) } } },
    { "Madame Aroma",
      { { DAY_1 | DAY_2, "10:00 às 20:00",
          "Mayor's Residence (East Clock Town): fale com ela. Recompensa: Kafei's Mask",
          NOTEBOOK_FLAG(RECEIVED_KAFEIS_MASK) },
        { DAY_3, "Depois das 18:00", "Milk Bar (precisa da Romani's Mask): entregue a Priority Mail. Recompensa: "
                                     "Chateau Romani",
          NOTEBOOK_FLAG(DELIVERED_PRIORITY_MAIL) } } },
    { "Toto",
      { { DAY_1 | DAY_2, "22:00 às 5:00", "Milk Bar (precisa da Romani's Mask): toque no palco com as máscaras Deku, "
                                          "Goron e Zora. Recompensa: Circus Leader's Mask (dada pelo Gorman)",
          NOTEBOOK_FLAG(RECEIVED_CIRCUS_LEADERS_MASK) } } },
    { "Gorman",
      { { DAY_1 | DAY_2, "22:00 às 5:00", "Milk Bar: faça a apresentação do Toto (máscaras Deku, Goron e Zora). "
                                          "Recompensa: Circus Leader's Mask",
          NOTEBOOK_FLAG(RECEIVED_CIRCUS_LEADERS_MASK) } } },
    { "Postman",
      { { DAY_1 | DAY_2, "À noite", "Post Office (West Clock Town): pare o relógio em 10,00 s; a Bunny Hood mostra o "
                                    "tempo. Recompensa: Piece of Heart",
          NOTEBOOK_FLAG(RECEIVED_POSTMAN_HP) },
        { DAY_3, "À noite", "Post Office: entregue a Priority Mail para ele. Recompensa: Postman's Hat",
          NOTEBOOK_FLAG(RECEIVED_POSTMANS_HAT) } } },
    { "Rosa Sisters",
      { { DAY_1 | DAY_2, "18:00 às 6:00",
          "West Clock Town: use a Kamaro's Mask e dance para elas. Recompensa: Piece of Heart",
          NOTEBOOK_FLAG(RECEIVED_ROSA_SISTERS_HP) } } },
    { "Toilet Hand",
      { { ANY_DAY, "0:00 às 6:00",
          "Banheiro da Stock Pot Inn: dê um papel (um Title Deed ou uma carta). Recompensa: Piece of Heart",
          NOTEBOOK_FLAG(RECEIVED_TOILET_HAND_HP) } } },
    { "Avó da Anju",
      { { DAY_1, "Durante o dia", "Stock Pot Inn: ouça a história curta com a All-Night Mask e acerte a pergunta. "
                                  "Recompensa: Piece of Heart",
          NOTEBOOK_FLAG(RECEIVED_GRANDMA_SHORT_STORY_HP) },
        { DAY_2, "Durante o dia", "Ouça a história longa com a All-Night Mask e acerte a pergunta. Recompensa: Piece "
                                  "of Heart",
          NOTEBOOK_FLAG(RECEIVED_GRANDMA_LONG_STORY_HP) } } },
    { "Kamaro",
      { { ANY_DAY, "0:00 às 6:00", "Termina Field, norte: toque a Song of Healing. Recompensa: Kamaro's Mask",
          NOTEBOOK_FLAG(RECEIVED_KAMAROS_MASK) } } },
    { "Grog",
      { { ANY_DAY, "Durante o dia", "Cucco Shack (Romani Ranch): use a Bremen Mask e marche até os pintinhos virarem "
                                    "galinhas. Recompensa: Bunny Hood",
          NOTEBOOK_FLAG(RECEIVED_BUNNY_HOOD) } } },
    { "Gorman Brothers",
      { { DAY_1 | DAY_2, "Durante o dia",
          "Gorman Track (Milk Road): ganhe a corrida com a Epona. Recompensa: Garo's Mask",
          NOTEBOOK_FLAG(RECEIVED_GAROS_MASK) } } },
    { "Shiro",
      { { ANY_DAY, "Qualquer hora",
          "Road to Ikana: veja-o com a Lens of Truth e dê uma Red Potion. Recompensa: Stone Mask",
          NOTEBOOK_FLAG(RECEIVED_STONE_MASK) } } },
    { "Guru-Guru",
      { { DAY_1 | DAY_2, "18:00 às 6:00", "Laundry Pool: ouça a história dele. Recompensa: Bremen Mask",
          NOTEBOOK_FLAG(RECEIVED_BREMEN_MASK) } } },
};

static bool IsStepDone(const GuideStep& step) {
    return (step.doneFlag != 0) && (gPlayState != nullptr) && CHECK_WEEKEVENTREG(step.doneFlag);
}

// Like the notebook, which only fills a row once Link has met that person.
static bool HasMetPerson(s32 person) {
    return (gPlayState != nullptr) && CHECK_WEEKEVENTREG(gBombersNotebookWeekEventFlags[person]);
}

static s32 GetCurrentDay() {
    return (gPlayState != nullptr) ? CURRENT_DAY : 0;
}

// Steps possible on every day are listed once, under "Qualquer dia"; the others under each of their days.
static const u8 sDayGroups[] = { DAY_1, DAY_2, DAY_3, ANY_DAY };

static std::string GetDayGroupTitle(u8 group, s32 currentDay) {
    if (group == ANY_DAY) {
        return "Qualquer dia";
    }
    s32 day = (group == DAY_1) ? 1 : (group == DAY_2) ? 2 : 3;
    return fmt::format("Dia {}{}", day, (day == currentDay) ? " (hoje)" : "");
}

static bool StepInGroup(const GuideStep& step, u8 group) {
    return (group == ANY_DAY) ? (step.days == ANY_DAY) : ((step.days != ANY_DAY) && (step.days & group));
}

// A day already gone in this cycle can only be done again after the Song of Time.
static bool IsGroupPast(u8 group, s32 currentDay) {
    return (group != ANY_DAY) && (currentDay > 0) && ((1 << currentDay) > group);
}

static s32 GetSelectedPerson() {
    if (gPlayState == nullptr || !sBombersNotebookOpen ||
        sBombersNotebook.loadState != BOMBERS_NOTEBOOK_LOAD_STATE_DONE) {
        return -1;
    }
    s32 person = sBombersNotebook.cursorPageRow + sBombersNotebook.cursorPage;
    return (person >= 0 && person < BOMBERS_NOTEBOOK_PERSON_MAX) ? person : -1;
}

static const ImVec4 sNoteYellow = ImVec4(1.0f, 0.93f, 0.55f, 0.96f);
static const ImVec4 sNoteGreen = ImVec4(0.62f, 0.9f, 0.6f, 0.96f);
static const ImVec4 sNotePast = ImVec4(0.78f, 0.75f, 0.62f, 0.85f);
static const ImVec4 sNoteInk = ImVec4(0.22f, 0.16f, 0.06f, 1.0f);
static const ImVec4 sTodayText = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
static const ImVec4 sDoneText = ImVec4(0.5f, 0.9f, 0.5f, 1.0f);

// One sticky note per step: yellow while it is still to do, green once the notebook has recorded it, and faded
// when its day has already passed in this cycle.
static void DrawStepNote(const GuideStep& step, bool past) {
    bool done = IsStepDone(step);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, done ? sNoteGreen : past ? sNotePast : sNoteYellow);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Text, sNoteInk);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushID(&step);
    ImGui::BeginChild("note", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::PushTextWrapPos(0.0f);
    if (done) {
        ImGui::TextWrapped("[OK] %s", step.task);
    } else {
        ImGui::TextWrapped("[!] %s", step.time);
        ImGui::TextWrapped("%s", step.task);
        if (past) {
            ImGui::TextWrapped("Esse dia já passou: só no próximo ciclo.");
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

// The person's steps under Dia 1, Dia 2, Dia 3 and Qualquer dia, as sticky notes, with today highlighted.
static void DrawPersonDays(const GuidePerson& person) {
    s32 currentDay = GetCurrentDay();
    for (u8 group : sDayGroups) {
        bool any = false;
        for (const GuideStep& step : person.steps) {
            any |= StepInGroup(step, group);
        }
        if (!any) {
            continue;
        }
        bool past = IsGroupPast(group, currentDay);
        bool today = (group != ANY_DAY) && (group == (1 << currentDay));
        ImGui::PushStyleColor(ImGuiCol_Text, today ? sTodayText
                                             : past ? ImVec4(0.7f, 0.7f, 0.7f, 1.0f)
                                                    : ImVec4(1.0f, 0.95f, 0.75f, 1.0f));
        ImGui::TextUnformatted(GetDayGroupTitle(group, currentDay).c_str());
        ImGui::PopStyleColor();
        for (const GuideStep& step : person.steps) {
            if (StepInGroup(step, group)) {
                DrawStepNote(step, past);
            }
        }
    }
}

// Draws the tips over the game's own Bombers' Notebook screen. The notebook paper fills the whole game view, so on
// wide screens the notes sit in the empty paper to the right of the schedule; on narrower ones they float over
// its bottom-right corner. They take no input, so touches and clicks still reach the game.
static void DrawNotesInNotebook() {
    if (!CVarGetInteger(CVAR_NAME, 1)) {
        return;
    }
    s32 selected = GetSelectedPerson();
    // The game's own text box (opened with A on an entry) uses the bottom of the screen.
    if (selected < 0 || !HasMetPerson(selected) || gPlayState->msgCtx.msgMode != MSGMODE_NONE) {
        return;
    }

    ImGuiWindow* gameWindow = ImGui::FindWindowByName("Main Game");
    if (gameWindow == nullptr || gameWindow->Hidden) {
        return;
    }
    ImVec2 gamePos = gameWindow->InnerRect.Min;
    ImVec2 gameSize = gameWindow->InnerRect.GetSize();
    if (gameSize.x <= 0.0f || gameSize.y <= 0.0f) {
        return;
    }

    // The schedule itself is drawn in the centred 4:3 area.
    float scheduleWidth = std::min(gameSize.x, gameSize.y * 4.0f / 3.0f);
    float sideMargin = (gameSize.x - scheduleWidth) / 2.0f;
    float pad = gameSize.y * 0.02f;
    ImVec2 notePos;
    float noteWidth;
    if (sideMargin >= gameSize.y * 0.3f) {
        noteWidth = sideMargin - pad * 2.0f;
        notePos = ImVec2(gamePos.x + gameSize.x - sideMargin + pad, gamePos.y + gameSize.y * 0.18f);
    } else {
        noteWidth = scheduleWidth * 0.42f;
        notePos = ImVec2(gamePos.x + sideMargin + scheduleWidth - noteWidth - pad, gamePos.y + gameSize.y * 0.45f);
    }

    ImGui::SetNextWindowPos(notePos);
    ImGui::SetNextWindowSize(ImVec2(noteWidth, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##BombersNotebookNotes", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);
    // Keep the notes inside the notebook at the large Android UI scale too.
    float fontScale = std::clamp(gameSize.y / (ImGui::GetFontSize() * 30.0f), 0.5f, 1.0f);
    ImGui::SetWindowFontScale(fontScale);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.75f, 1.0f));
    ImGui::TextWrapped("Dicas: %s", sGuide[selected].name);
    ImGui::PopStyleColor();
    DrawPersonDays(sGuide[selected]);
    ImGui::End();
}

void BombersNotebookGuideWindow::InitElement() {
}

void BombersNotebookGuideWindow::UpdateElement() {
}

void BombersNotebookGuideWindow::Draw() {
    DrawNotesInNotebook();
    GuiWindow::Draw();
}

// ---------------------------------------------------------------------------------------------------------------
// Quest menu: a notebook-styled window with a section list on the left, the entries of the section in the middle
// (checked once done, read from the save) and the selected entry's details on the right. Pictures come from the
// player's own game files: the notebook portraits and the item icons.

extern "C" {
extern TexturePtr gBombersNotebookPhotos[24];
}

namespace {

enum QuestSection { SECTION_PEOPLE, SECTION_MASKS, SECTION_SONGS, SECTION_FAIRIES, SECTION_MAX };

struct SimpleEntry {
    s32 id; // ItemId (masks), QuestItem (songs) or fairy group index
    const char* name;
    const char* where;
    const char* when;
};

const SimpleEntry sMasks[] = {
    { ITEM_MASK_DEKU, "Máscara Deku", "Torre do Relógio: toque a Canção da Cura para o Vendedor de Máscaras depois "
                                      "de recuperar a ocarina.", "Dia 1 a Dia 3" },
    { ITEM_MASK_GORON, "Máscara Goron", "Cemitério Goron (Mountain Village): toque a Canção da Cura para o fantasma "
                                        "do Darmani.", "Qualquer dia" },
    { ITEM_MASK_ZORA, "Máscara Zora", "Great Bay Coast: empurre o Mikau até a praia e toque a Canção da Cura.",
      "Qualquer dia" },
    { ITEM_MASK_FIERCE_DEITY, "Máscara da Divindade Feroz", "Na Lua: dê todas as outras máscaras para as crianças.",
      "Fim do Dia 3" },
    { ITEM_MASK_TRUTH, "Máscara da Verdade", "Swamp Spider House: destrua as 30 Gold Skulltulas.", "Qualquer dia" },
    { ITEM_MASK_KAFEIS_MASK, "Máscara do Kafei", "Mayor's Residence: fale com a Madame Aroma.", "Dia 1 e Dia 2, de dia" },
    { ITEM_MASK_ALL_NIGHT, "Máscara da Noite Toda", "Curiosity Shop: compre por 500 rúpias depois de salvar a Bomb Shop "
                                                    "Lady.", "Dia 3, 22:00 às 6:00" },
    { ITEM_MASK_BUNNY, "Capuz de Coelho", "Cucco Shack: use a Bremen Mask até os pintinhos virarem galinhas.",
      "Qualquer dia, de dia" },
    { ITEM_MASK_KEATON, "Máscara Keaton", "Sala dos fundos da Curiosity Shop (Laundry Pool), depois que o Kafei sai.",
      "Dia 3, 13:00 às 22:00" },
    { ITEM_MASK_GARO, "Máscara do Garo", "Gorman Track: ganhe a corrida dos irmãos Gorman com a Epona.",
      "Dia 1 e Dia 2, de dia" },
    { ITEM_MASK_ROMANI, "Máscara da Romani", "Romani Ranch: proteja a carroça da Cremia até Clock Town.",
      "Dia 2, 18:00" },
    { ITEM_MASK_CIRCUS_LEADER, "Máscara do Líder do Circo", "Milk Bar: toque no palco com as máscaras Deku, Goron e "
                                                            "Zora.", "Dia 1 e Dia 2, 22:00 às 5:00" },
    { ITEM_MASK_POSTMAN, "Chapéu do Carteiro", "Post Office: entregue a Priority Mail para o carteiro.",
      "Dia 3, à noite" },
    { ITEM_MASK_COUPLE, "Máscara do Casal", "Stock Pot Inn: espere a Anju e o Kafei se reencontrarem.",
      "Dia 3, 4:00 às 6:00" },
    { ITEM_MASK_GREAT_FAIRY, "Máscara da Grande Fada", "Fonte da Grande Fada de Clock Town: devolva a fada perdida "
                                                       "em forma humana.", "Qualquer dia" },
    { ITEM_MASK_GIBDO, "Máscara Gibdo", "Music Box House (Ikana Canyon): toque a Canção da Cura para o pai da Pamela.",
      "Qualquer dia" },
    { ITEM_MASK_DON_GERO, "Máscara do Don Gero", "Mountain Village: leve o Rock Sirloin para o Goron faminto.",
      "Qualquer dia" },
    { ITEM_MASK_KAMARO, "Máscara do Kamaro", "Termina Field, ao norte: toque a Canção da Cura para o Kamaro.",
      "Qualquer dia, 0:00 às 6:00" },
    { ITEM_MASK_CAPTAIN, "Chapéu do Capitão", "Cemitério de Ikana: acorde e derrote o Capitão Keeta com a Sonata.",
      "Dia 1, à noite" },
    { ITEM_MASK_STONE, "Máscara de Pedra", "Road to Ikana: veja o Shiro com a Lente da Verdade e dê uma Poção Vermelha.",
      "Qualquer hora" },
    { ITEM_MASK_BREMEN, "Máscara Bremen", "Laundry Pool: ouça a história do Guru-Guru.",
      "Dia 1 e Dia 2, 18:00 às 6:00" },
    { ITEM_MASK_BLAST, "Máscara Explosiva", "North Clock Town: impeça o Sakon de roubar a Bomb Shop Lady.",
      "Dia 1, 0:00" },
    { ITEM_MASK_SCENTS, "Máscara dos Aromas", "Deku Palace: vença a corrida do Mordomo Deku depois do Woodfall Temple.",
      "Qualquer dia" },
    { ITEM_MASK_GIANT, "Máscara do Gigante", "Stone Tower Temple (ou na luta contra o Twinmold, com a opção do MM3D).",
      "Qualquer dia" },
};

const SimpleEntry sSongs[] = {
    { QUEST_SONG_TIME, "Canção do Tempo", "Topo da Torre do Relógio: recupere a ocarina do Skull Kid.", "Dia 3, 0:00" },
    { QUEST_SONG_HEALING, "Canção da Cura", "Torre do Relógio: o Vendedor de Máscaras ensina.", "Qualquer dia" },
    { QUEST_SONG_SOARING, "Canção do Voo", "Woodfall: leia a pedra perto da entrada do templo.", "Qualquer dia" },
    { QUEST_SONG_EPONA, "Canção da Epona", "Romani Ranch: a Romani ensina depois do treino com o arco.",
      "Dia 1, antes das 18:00" },
    { QUEST_SONG_STORMS, "Canção das Tempestades", "Cemitério de Ikana, embaixo de um túmulo: o fantasma do Flat ensina.",
      "Dia 2, à noite" },
    { QUEST_SONG_SONATA, "Sonata do Despertar", "Deku Palace: o macaco preso ensina.", "Qualquer dia" },
    { QUEST_SONG_LULLABY, "Canção de Ninar Goron", "Goron Shrine e Mountain Village: o ancião começa, o filho dele "
                                                   "termina.", "Qualquer dia" },
    { QUEST_SONG_BOSSA_NOVA, "Nova Onda Bossa Nova", "Marine Research Lab: devolva os ovos da Lulu.", "Qualquer dia" },
    { QUEST_SONG_ELEGY, "Elegia do Vazio", "Ancient Castle of Ikana: derrote o Igos du Ikana.", "Qualquer dia" },
    { QUEST_SONG_OATH, "Juramento à Ordem", "Os Gigantes ensinam depois do primeiro templo.", "Qualquer dia" },
};

const SimpleEntry sFairies[] = {
    { -1, "Clock Town", "Laundry Pool (dia) ou East Clock Town (noite): leve a fada para a fonte em North Clock Town.",
      "Qualquer dia" },
    { 0, "Woodfall Temple", "15 fadas no templo. A fonte fica em Woodfall.", "Qualquer dia" },
    { 1, "Snowhead Temple", "15 fadas no templo. A fonte fica na Mountain Village.", "Qualquer dia" },
    { 2, "Great Bay Temple", "15 fadas no templo. A fonte fica na Zora Cape.", "Qualquer dia" },
    { 3, "Stone Tower Temple", "15 fadas no templo. A fonte fica em Ikana Canyon.", "Qualquer dia" },
};

const ImVec4 kFrame = ImVec4(0.17f, 0.07f, 0.26f, 1.0f);
const ImVec4 kFrameLight = ImVec4(0.29f, 0.14f, 0.40f, 1.0f);
const ImVec4 kGold = ImVec4(0.96f, 0.80f, 0.38f, 1.0f);
const ImVec4 kParchment = ImVec4(0.93f, 0.87f, 0.74f, 1.0f);
const ImVec4 kInk = ImVec4(0.24f, 0.15f, 0.08f, 1.0f);
const ImVec4 kInkTitle = ImVec4(0.42f, 0.13f, 0.20f, 1.0f);
const ImVec4 kCheckGreen = ImVec4(0.35f, 0.85f, 0.30f, 1.0f);

int sSection = SECTION_PEOPLE;
int sSelected[SECTION_MAX] = { 1, 0, 0, 0 };

ImTextureID Texture(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
    return Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(path);
}

const char* PersonPhoto(s32 person) {
    // The photo list starts with Anju; the Bombers' photo comes after the other 19 people.
    return (const char*)gBombersNotebookPhotos[person == BOMBERS_NOTEBOOK_PERSON_BOMBERS ? 19 : person - 1];
}

void Picture(const char* path, float size, float alpha = 1.0f) {
    ImTextureID id = Texture(path);
    if (id != nullptr) {
        ImGui::ImageWithBg(id, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, alpha));
    } else {
        ImGui::Dummy(ImVec2(size, size));
    }
}

// A box with a green tick when done, drawn so it does not depend on the font having a check mark.
void CheckBox(bool done, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(p, ImVec2(p.x + size, p.y + size), ImGui::GetColorU32(kGold), 3.0f, 0, 2.0f);
    if (done) {
        ImVec2 a(p.x + size * 0.18f, p.y + size * 0.52f);
        ImVec2 b(p.x + size * 0.42f, p.y + size * 0.78f);
        ImVec2 c(p.x + size * 0.86f, p.y + size * 0.16f);
        draw->AddLine(a, b, ImGui::GetColorU32(kCheckGreen), size * 0.18f);
        draw->AddLine(b, c, ImGui::GetColorU32(kCheckGreen), size * 0.18f);
    }
    ImGui::Dummy(ImVec2(size, size));
}

bool PersonDone(s32 person) {
    for (const GuideStep& step : sGuide[person].steps) {
        if (!IsStepDone(step)) {
            return false;
        }
    }
    return true;
}

bool SimpleDone(int section, const SimpleEntry& entry) {
    if (gPlayState == nullptr) {
        return false;
    }
    switch (section) {
        case SECTION_MASKS:
            return INV_CONTENT(entry.id) == entry.id;
        case SECTION_SONGS:
            return CHECK_QUEST_ITEM(entry.id);
        case SECTION_FAIRIES:
            return (entry.id < 0) ? CHECK_WEEKEVENTREG(WEEKEVENTREG_08_80)
                                  : (gSaveContext.save.saveInfo.inventory.strayFairies[entry.id] >= 15);
    }
    return false;
}

const char* SimpleIcon(int section, const SimpleEntry& entry) {
    switch (section) {
        case SECTION_MASKS:
            return (const char*)gItemIcons[entry.id];
        case SECTION_SONGS:
            return (const char*)gItemIcons[ITEM_SONG_SONATA + (entry.id - QUEST_SONG_SONATA)];
        default:
            return (const char*)gItemIcons[ITEM_MASK_GREAT_FAIRY];
    }
}

const SimpleEntry* SimpleList(int section, int& count) {
    switch (section) {
        case SECTION_MASKS:
            count = ARRAY_COUNT(sMasks);
            return sMasks;
        case SECTION_SONGS:
            count = ARRAY_COUNT(sSongs);
            return sSongs;
        case SECTION_FAIRIES:
            count = ARRAY_COUNT(sFairies);
            return sFairies;
    }
    count = 0;
    return nullptr;
}

void SectionTitle(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
}

void DetailHeading(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kInkTitle);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

// Pulls "Recompensa: ..." out of a step's text.
std::string RewardOf(const char* task) {
    const char* r = strstr(task, "Recompensa: ");
    if (r == nullptr) {
        return "";
    }
    return std::string(r + strlen("Recompensa: "));
}

std::string DaysText(u8 days) {
    if (days == ANY_DAY) {
        return "Qualquer dia";
    }
    std::string out;
    for (int d = 1; d <= 3; d++) {
        if (days & (1 << d)) {
            out += out.empty() ? fmt::format("Dia {}", d) : fmt::format(", Dia {}", d);
        }
    }
    return out;
}

void DrawPersonDetail(s32 person) {
    if (!HasMetPerson(person)) {
        DetailHeading("???");
        ImGui::TextWrapped("Você ainda não conheceu esta pessoa. O Caderno registra cada um quando vocês se encontram.");
        return;
    }
    const GuidePerson& guide = sGuide[person];
    Picture(PersonPhoto(person), 72.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.35f);
    DetailHeading(guide.name);
    ImGui::SetWindowFontScale(1.0f);
    s32 done = 0;
    for (const GuideStep& step : guide.steps) {
        done += IsStepDone(step);
    }
    ImGui::Text("%d de %d etapas feitas", done, (int)guide.steps.size());
    ImGui::EndGroup();
    ImGui::Separator();

    DetailHeading("Progresso da missão");
    s32 currentDay = GetCurrentDay();
    for (const GuideStep& step : guide.steps) {
        bool stepDone = IsStepDone(step);
        CheckBox(stepDone, 18.0f);
        ImGui::SameLine();
        ImGui::BeginGroup();
        // Every day the step can be done on is already gone in this cycle.
        bool past = !stepDone && step.days != ANY_DAY && currentDay > 0 && (step.days >> currentDay) == 0;
        ImGui::PushStyleColor(ImGuiCol_Text, past ? ImVec4(0.5f, 0.45f, 0.4f, 1.0f) : kInk);
        ImGui::TextWrapped("%s - %s", DaysText(step.days).c_str(), step.time);
        ImGui::TextWrapped("%s", step.task);
        if (past) {
            ImGui::TextWrapped("Esse dia já passou: só no próximo ciclo.");
        }
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Spacing();
    }

    std::vector<std::string> rewards;
    for (const GuideStep& step : guide.steps) {
        std::string reward = RewardOf(step.task);
        if (!reward.empty() && std::find(rewards.begin(), rewards.end(), reward) == rewards.end()) {
            rewards.push_back(reward);
        }
    }
    if (!rewards.empty()) {
        ImGui::Separator();
        DetailHeading("Recompensa");
        for (const std::string& reward : rewards) {
            ImGui::BulletText("%s", reward.c_str());
        }
    }
}

void DrawSimpleDetail(int section, const SimpleEntry& entry) {
    bool done = SimpleDone(section, entry);
    Picture(SimpleIcon(section, entry), 64.0f, done ? 1.0f : 0.45f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.35f);
    DetailHeading(entry.name);
    ImGui::SetWindowFontScale(1.0f);
    if (section == SECTION_FAIRIES && entry.id >= 0 && gPlayState != nullptr) {
        ImGui::Text("%d de 15 fadas", (int)gSaveContext.save.saveInfo.inventory.strayFairies[entry.id]);
    } else {
        ImGui::TextUnformatted(done ? "Já conseguiu" : "Ainda não conseguiu");
    }
    ImGui::EndGroup();
    ImGui::Separator();
    DetailHeading("Horário");
    ImGui::TextWrapped("%s", entry.when);
    ImGui::Spacing();
    DetailHeading("Local");
    ImGui::TextWrapped("%s", entry.where);
}

} // namespace

void BombersNotebookGuideWindow::DrawElement() {
    ImGui::SetNextWindowSize(ImVec2(980, 600), ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kFrame);
    ImGui::PushStyleColor(ImGuiCol_Border, kGold);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    if (!ImGui::BeginChild("Bombers Notebook Guide", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        return;
    }

    // Title bar: name in the middle, day and time on the right.
    ImGui::SetWindowFontScale(1.5f);
    const char* title = "Caderno dos Bombers";
    float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    if (gPlayState != nullptr) {
        s32 minutes = (s32)TIME_TO_MINUTES_F(CURRENT_TIME);
        std::string clock = fmt::format("Dia {}   {:02d}:{:02d}", CURRENT_DAY, minutes / 60, minutes % 60);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(clock.c_str()).x - 16.0f);
        ImGui::TextUnformatted(clock.c_str());
    }
    UIWidgets::CVarCheckbox("Mostrar dicas dentro do Caderno do jogo", CVAR_NAME,
                            UIWidgets::CheckboxOptions().DefaultValue(true));

    float height = ImGui::GetContentRegionAvail().y;

    // Sections
    static const char* sSectionNames[SECTION_MAX] = { "Pessoas", "Máscaras", "Canções", "Fadas Perdidas" };
    static const s32 sSectionIcons[SECTION_MAX] = { ITEM_BOMBERS_NOTEBOOK, ITEM_MASK_KEATON, ITEM_OCARINA_OF_TIME,
                                                    ITEM_MASK_GREAT_FAIRY };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kFrameLight);
    ImGui::BeginChild("sections", ImVec2(180, height), ImGuiChildFlags_Borders);
    for (int i = 0; i < SECTION_MAX; i++) {
        ImGui::PushID(i);
        bool active = (sSection == i);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.55f, 0.33f, 0.12f, 1.0f) : kFrame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.25f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? kGold : ImVec4(0.95f, 0.9f, 1.0f, 1.0f));
        ImVec2 start = ImGui::GetCursorPos();
        if (ImGui::Button("##section", ImVec2(-1, 44))) {
            sSection = i;
        }
        ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(start.x + 6, start.y + 6));
        Picture((const char*)gItemIcons[sSectionIcons[i]], 32.0f);
        ImGui::SameLine();
        ImGui::SetCursorPosY(start.y + 13);
        ImGui::TextUnformatted(sSectionNames[i]);
        ImGui::SetCursorPos(after);
        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Entries of the section
    ImGui::BeginChild("entries", ImVec2(320, height), ImGuiChildFlags_Borders);
    int total = 0, finished = 0;
    if (sSection == SECTION_PEOPLE) {
        SectionTitle("Pessoas de Termina");
        for (s32 i = 0; i < BOMBERS_NOTEBOOK_PERSON_MAX; i++) {
            bool met = HasMetPerson(i);
            bool done = met && PersonDone(i);
            total++;
            finished += done;
            ImGui::PushID(i);
            ImVec2 start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##person", sSelected[SECTION_PEOPLE] == i, 0, ImVec2(0, 40))) {
                sSelected[SECTION_PEOPLE] = i;
            }
            ImVec2 after = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(start.x + 4, start.y + 2));
            Picture(PersonPhoto(i), 36.0f, met ? 1.0f : 0.25f);
            ImGui::SameLine();
            ImGui::SetCursorPosY(start.y + 11);
            ImGui::TextUnformatted(met ? sGuide[i].name : "???");
            ImGui::SameLine(ImGui::GetWindowWidth() - 40);
            ImGui::SetCursorPosY(start.y + 10);
            CheckBox(done, 20.0f);
            ImGui::SetCursorPos(after);
            ImGui::PopID();
        }
    } else {
        SectionTitle(sSectionNames[sSection]);
        int count = 0;
        const SimpleEntry* list = SimpleList(sSection, count);
        for (int i = 0; i < count; i++) {
            bool done = SimpleDone(sSection, list[i]);
            total++;
            finished += done;
            ImGui::PushID(i);
            ImVec2 start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##entry", sSelected[sSection] == i, 0, ImVec2(0, 40))) {
                sSelected[sSection] = i;
            }
            ImVec2 after = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(start.x + 4, start.y + 2));
            Picture(SimpleIcon(sSection, list[i]), 36.0f, done ? 1.0f : 0.35f);
            ImGui::SameLine();
            ImGui::SetCursorPosY(start.y + 11);
            ImGui::TextUnformatted(list[i].name);
            ImGui::SameLine(ImGui::GetWindowWidth() - 40);
            ImGui::SetCursorPosY(start.y + 10);
            CheckBox(done, 20.0f);
            ImGui::SetCursorPos(after);
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    ImGui::Text("%d de %d completos", finished, total);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Details, on parchment
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kParchment);
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.6f, 0.45f, 0.3f, 1.0f));
    ImGui::BeginChild("details", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (sSection == SECTION_PEOPLE) {
        DrawPersonDetail(std::clamp(sSelected[SECTION_PEOPLE], 0, BOMBERS_NOTEBOOK_PERSON_MAX - 1));
    } else {
        int count = 0;
        const SimpleEntry* list = SimpleList(sSection, count);
        if (count > 0) {
            DrawSimpleDetail(sSection, list[std::clamp(sSelected[sSection], 0, count - 1)]);
        }
    }
    if (gPlayState == nullptr) {
        ImGui::Spacing();
        ImGui::TextWrapped("Carregue um save para ver o seu progresso.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}
