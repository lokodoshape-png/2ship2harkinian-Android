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
#include <cfloat>
#include <cmath>
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
    // N on the keyboard, or Back / View / Select / - on a controller, opens and closes the quest menu. Neither is
    // used by the game's default controls.
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_N, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))) {
        ToggleVisibility();
    }
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

// What each person's story is about and where to find them, shown at the top of their page.
struct PersonInfo {
    const char* summary;
    const char* place;
};

const PersonInfo sPeopleInfo[BOMBERS_NOTEBOOK_PERSON_MAX] = {
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

// ---------------------------------------------------------------------------------------------------------------
// Drawing. Everything is laid out on a 1100x700 page and scaled to the window, so it keeps its look at any size.

constexpr float kPageW = 1100.0f;
constexpr float kPageH = 700.0f;
constexpr int kRowsPerPage = 8;

ImU32 Col(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

const ImU32 cGold = Col(244, 204, 98);
const ImU32 cGoldDark = Col(150, 105, 40);
const ImU32 cPurpleDeep = Col(28, 10, 44);
const ImU32 cPurple = Col(58, 26, 86);
const ImU32 cPurpleLight = Col(92, 50, 128);
const ImU32 cParchment = Col(240, 226, 196);
const ImU32 cParchmentDark = Col(222, 202, 160);
const ImU32 cInk = Col(62, 38, 22);
const ImU32 cWine = Col(104, 30, 48);
const ImU32 cGreen = Col(84, 214, 70);

struct Page {
    ImDrawList* dl;
    ImVec2 origin;
    float k;
    ImVec2 P(float x, float y) const {
        return ImVec2(origin.x + x * k, origin.y + y * k);
    }
    float S(float v) const {
        return v * k;
    }
};

void Text(const Page& pg, float x, float y, float size, ImU32 col, const char* text, float wrap = 0.0f) {
    pg.dl->AddText(ImGui::GetFont(), pg.S(size), pg.P(x, y), col, text, nullptr, wrap > 0 ? pg.S(wrap) : 0.0f);
}

void TextShadow(const Page& pg, float x, float y, float size, ImU32 col, const char* text) {
    Text(pg, x + 1.5f, y + 2.0f, size, Col(0, 0, 0, 160), text);
    Text(pg, x, y, size, col, text);
}

float TextWidth(const Page& pg, float size, const char* text) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void TextCentered(const Page& pg, float cx, float y, float size, ImU32 col, const char* text, bool shadow = true) {
    float w = TextWidth(pg, size, text);
    if (shadow) {
        TextShadow(pg, cx - w / 2, y, size, col, text);
    } else {
        Text(pg, cx - w / 2, y, size, col, text);
    }
}

float WrappedHeight(const Page& pg, float size, float wrap, const char* text) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, wrap, text).y;
}

// A gold frame: dark outline, bright gold band, thin highlight, with optional glow around it.
void GoldFrame(const Page& pg, ImVec2 a, ImVec2 b, float rounding, float width, bool glow = false) {
    ImDrawList* dl = pg.dl;
    if (glow) {
        for (int i = 5; i >= 1; i--) {
            dl->AddRect(pg.P(a.x - i * 2, a.y - i * 2), pg.P(b.x + i * 2, b.y + i * 2), Col(255, 210, 90, 22 * (6 - i)),
                        pg.S(rounding + i * 2), 0, pg.S(3.0f));
        }
    }
    dl->AddRect(pg.P(a.x, a.y), pg.P(b.x, b.y), Col(40, 20, 10, 230), pg.S(rounding), 0, pg.S(width + 2));
    dl->AddRect(pg.P(a.x, a.y), pg.P(b.x, b.y), cGold, pg.S(rounding), 0, pg.S(width));
    dl->AddRect(pg.P(a.x + width * 0.5f, a.y + width * 0.5f), pg.P(b.x - width * 0.5f, b.y - width * 0.5f),
                Col(255, 240, 180, 120), pg.S(rounding), 0, pg.S(1.0f));
}

void Gradient(const Page& pg, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom) {
    pg.dl->AddRectFilledMultiColor(pg.P(a.x, a.y), pg.P(b.x, b.y), top, top, bottom, bottom);
}

void Panel(const Page& pg, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding) {
    // Rounded fill with a vertical gradient: a rounded base plus the gradient inset by the rounding.
    pg.dl->AddRectFilled(pg.P(a.x, a.y), pg.P(b.x, b.y), bottom, pg.S(rounding));
    Gradient(pg, ImVec2(a.x + rounding * 0.3f, a.y + rounding * 0.3f), ImVec2(b.x - rounding * 0.3f, b.y - rounding),
             top, bottom);
}

// Little diamond studs at the corners of a frame.
void CornerStuds(const Page& pg, ImVec2 a, ImVec2 b, float inset) {
    ImVec2 corners[4] = { { a.x + inset, a.y + inset }, { b.x - inset, a.y + inset }, { a.x + inset, b.y - inset },
                          { b.x - inset, b.y - inset } };
    for (ImVec2 c : corners) {
        float r = 7.0f;
        pg.dl->AddQuadFilled(pg.P(c.x, c.y - r), pg.P(c.x + r, c.y), pg.P(c.x, c.y + r), pg.P(c.x - r, c.y), cGold);
        pg.dl->AddQuadFilled(pg.P(c.x, c.y - r * 0.45f), pg.P(c.x + r * 0.45f, c.y), pg.P(c.x, c.y + r * 0.45f),
                             pg.P(c.x - r * 0.45f, c.y), Col(120, 40, 140));
    }
}

// ----- Icons, drawn with shapes so they are crisp at any size. (cx, cy) is the centre, s the size.

void IconTriforce(const Page& pg, float cx, float cy, float s, ImU32 col = cGold) {
    float h = s * 0.866f;
    auto tri = [&](float x, float y, float w) {
        float th = w * 0.866f;
        pg.dl->AddTriangleFilled(pg.P(x, y - th / 2), pg.P(x + w / 2, y + th / 2), pg.P(x - w / 2, y + th / 2), col);
    };
    tri(cx, cy - h / 4, s / 2);
    tri(cx - s / 4, cy + h / 4, s / 2);
    tri(cx + s / 4, cy + h / 4, s / 2);
}

void IconMajora(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    // Spikes
    for (int i = 0; i < 10; i++) {
        float ang = 3.14159f * (0.05f + 0.9f * i / 9.0f) + 3.14159f;
        float bx = cx + cosf(ang) * s * 0.36f, by = cy + sinf(ang) * s * 0.30f;
        float tx = cx + cosf(ang) * s * 0.62f, ty = cy + sinf(ang) * s * 0.55f;
        float px = -sinf(ang) * s * 0.06f, py = cosf(ang) * s * 0.06f;
        dl->AddTriangleFilled(pg.P(bx + px, by + py), pg.P(tx, ty), pg.P(bx - px, by - py), Col(236, 196, 84));
    }
    for (int i = 0; i < 2; i++) {
        float side = i == 0 ? -1.0f : 1.0f;
        dl->AddTriangleFilled(pg.P(cx + side * s * 0.12f, cy + s * 0.25f), pg.P(cx + side * s * 0.42f, cy + s * 0.55f),
                              pg.P(cx + side * s * 0.30f, cy + s * 0.18f), Col(80, 170, 70));
    }
    // Heart-shaped face
    dl->AddCircleFilled(pg.P(cx - s * 0.17f, cy - s * 0.02f), pg.S(s * 0.24f), Col(120, 40, 150));
    dl->AddCircleFilled(pg.P(cx + s * 0.17f, cy - s * 0.02f), pg.S(s * 0.24f), Col(120, 40, 150));
    dl->AddTriangleFilled(pg.P(cx - s * 0.40f, cy + s * 0.04f), pg.P(cx + s * 0.40f, cy + s * 0.04f),
                          pg.P(cx, cy + s * 0.42f), Col(120, 40, 150));
    dl->AddTriangleFilled(pg.P(cx - s * 0.30f, cy + s * 0.12f), pg.P(cx + s * 0.30f, cy + s * 0.12f),
                          pg.P(cx, cy + s * 0.36f), Col(200, 50, 60));
    for (int i = 0; i < 2; i++) {
        float ex = cx + (i == 0 ? -1 : 1) * s * 0.16f;
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.13f), Col(250, 200, 40));
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.085f), Col(240, 110, 30));
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.04f), Col(60, 170, 60));
    }
}

void IconBook(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddQuadFilled(pg.P(cx - s * 0.45f, cy - s * 0.30f), pg.P(cx, cy - s * 0.22f), pg.P(cx, cy + s * 0.34f),
                      pg.P(cx - s * 0.45f, cy + s * 0.26f), Col(246, 232, 196));
    dl->AddQuadFilled(pg.P(cx, cy - s * 0.22f), pg.P(cx + s * 0.45f, cy - s * 0.30f), pg.P(cx + s * 0.45f, cy + s * 0.26f),
                      pg.P(cx, cy + s * 0.34f), Col(232, 214, 172));
    for (int i = 0; i < 4; i++) {
        float y = cy - s * 0.12f + i * s * 0.09f;
        dl->AddLine(pg.P(cx - s * 0.36f, y - s * 0.03f), pg.P(cx - s * 0.06f, y), Col(150, 110, 60), pg.S(1.2f));
        dl->AddLine(pg.P(cx + s * 0.06f, y), pg.P(cx + s * 0.36f, y - s * 0.03f), Col(150, 110, 60), pg.S(1.2f));
    }
    dl->AddLine(pg.P(cx, cy - s * 0.22f), pg.P(cx, cy + s * 0.34f), Col(120, 70, 30), pg.S(2.0f));
    dl->AddRect(pg.P(cx - s * 0.47f, cy - s * 0.32f), pg.P(cx + s * 0.47f, cy + s * 0.36f), Col(140, 80, 30), pg.S(3),
                0, pg.S(2.0f));
}

void IconHeart(const Page& pg, float cx, float cy, float s, ImU32 col = Col(220, 40, 70)) {
    ImDrawList* dl = pg.dl;
    float r = s * 0.24f;
    dl->AddCircleFilled(pg.P(cx - r * 0.95f, cy - r * 0.3f), pg.S(r + 2), cGold);
    dl->AddCircleFilled(pg.P(cx + r * 0.95f, cy - r * 0.3f), pg.S(r + 2), cGold);
    dl->AddTriangleFilled(pg.P(cx - r * 2.05f, cy - r * 0.1f), pg.P(cx + r * 2.05f, cy - r * 0.1f),
                          pg.P(cx, cy + s * 0.46f), cGold);
    dl->AddCircleFilled(pg.P(cx - r * 0.95f, cy - r * 0.3f), pg.S(r), col);
    dl->AddCircleFilled(pg.P(cx + r * 0.95f, cy - r * 0.3f), pg.S(r), col);
    dl->AddTriangleFilled(pg.P(cx - r * 1.9f, cy - r * 0.1f), pg.P(cx + r * 1.9f, cy - r * 0.1f),
                          pg.P(cx, cy + s * 0.40f), col);
    dl->AddCircleFilled(pg.P(cx - r * 1.1f, cy - r * 0.55f), pg.S(r * 0.3f), Col(255, 200, 210, 200));
}

void IconFairy(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 4; i++) {
        float sx = (i % 2 == 0) ? -1.0f : 1.0f;
        float sy = (i < 2) ? -1.0f : 1.0f;
        ImVec2 c = pg.P(cx + sx * s * 0.24f, cy + sy * s * 0.14f);
        dl->AddEllipseFilled(c, ImVec2(pg.S(s * 0.22f), pg.S(s * (i < 2 ? 0.16f : 0.11f))), Col(230, 240, 255, 170),
                             sx * sy * 0.5f);
        dl->AddEllipse(c, ImVec2(pg.S(s * 0.22f), pg.S(s * (i < 2 ? 0.16f : 0.11f))), Col(255, 255, 255, 220),
                       sx * sy * 0.5f, 0, pg.S(1.0f));
    }
    for (int i = 6; i >= 1; i--) {
        dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.04f * i), Col(255, 240, 140, 30));
    }
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.12f), Col(255, 250, 200));
}

void IconNote(const Page& pg, float cx, float cy, float s, ImU32 col = Col(190, 140, 255)) {
    ImDrawList* dl = pg.dl;
    dl->AddEllipseFilled(pg.P(cx - s * 0.14f, cy + s * 0.24f), ImVec2(pg.S(s * 0.16f), pg.S(s * 0.11f)), col, -0.4f);
    dl->AddLine(pg.P(cx + s * 0.0f, cy + s * 0.22f), pg.P(cx + s * 0.0f, cy - s * 0.36f), col, pg.S(s * 0.07f));
    dl->AddBezierCubic(pg.P(cx, cy - s * 0.36f), pg.P(cx + s * 0.25f, cy - s * 0.25f), pg.P(cx + s * 0.32f, cy - s * 0.1f),
                       pg.P(cx + s * 0.20f, cy + s * 0.02f), col, pg.S(s * 0.07f));
}

void IconMaskFace(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddEllipseFilled(pg.P(cx, cy), ImVec2(pg.S(s * 0.36f), pg.S(s * 0.42f)), Col(210, 180, 230));
    dl->AddEllipse(pg.P(cx, cy), ImVec2(pg.S(s * 0.36f), pg.S(s * 0.42f)), cGold, 0, 0, pg.S(2.0f));
    dl->AddEllipseFilled(pg.P(cx - s * 0.14f, cy - s * 0.06f), ImVec2(pg.S(s * 0.09f), pg.S(s * 0.07f)), Col(70, 30, 90));
    dl->AddEllipseFilled(pg.P(cx + s * 0.14f, cy - s * 0.06f), ImVec2(pg.S(s * 0.09f), pg.S(s * 0.07f)), Col(70, 30, 90));
    dl->AddBezierCubic(pg.P(cx - s * 0.16f, cy + s * 0.16f), pg.P(cx - s * 0.06f, cy + s * 0.27f),
                       pg.P(cx + s * 0.06f, cy + s * 0.27f), pg.P(cx + s * 0.16f, cy + s * 0.16f), Col(70, 30, 90),
                       pg.S(2.5f));
}

void IconEnvelope(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    ImVec2 a = pg.P(cx - s * 0.42f, cy - s * 0.28f), b = pg.P(cx + s * 0.42f, cy + s * 0.28f);
    dl->AddRectFilled(a, b, Col(250, 244, 230), pg.S(3));
    dl->AddRect(a, b, Col(170, 140, 110), pg.S(3), 0, pg.S(1.5f));
    dl->AddLine(pg.P(cx - s * 0.42f, cy - s * 0.26f), pg.P(cx, cy + s * 0.04f), Col(170, 140, 110), pg.S(1.5f));
    dl->AddLine(pg.P(cx + s * 0.42f, cy - s * 0.26f), pg.P(cx, cy + s * 0.04f), Col(170, 140, 110), pg.S(1.5f));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.04f), pg.S(s * 0.09f), Col(190, 30, 40));
}

void IconPendant(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->PathArcTo(pg.P(cx, cy - s * 0.22f), pg.S(s * 0.26f), 3.4f, 6.0f);
    dl->PathStroke(Col(90, 80, 70), 0, pg.S(1.8f));
    IconHeart(pg, cx, cy + s * 0.08f, s * 0.62f, Col(140, 60, 190));
}

void IconSun(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 12; i++) {
        float a = i * 3.14159f / 6.0f;
        float px = -sinf(a) * s * 0.07f, py = cosf(a) * s * 0.07f;
        dl->AddTriangleFilled(pg.P(cx + cosf(a) * s * 0.26f + px, cy + sinf(a) * s * 0.26f + py),
                              pg.P(cx + cosf(a) * s * 0.46f, cy + sinf(a) * s * 0.46f),
                              pg.P(cx + cosf(a) * s * 0.26f - px, cy + sinf(a) * s * 0.26f - py), Col(240, 170, 40));
    }
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.28f), Col(250, 205, 60));
    dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.28f), Col(200, 120, 20), 0, pg.S(1.5f));
    dl->AddCircleFilled(pg.P(cx - s * 0.09f, cy - s * 0.04f), pg.S(s * 0.035f), Col(140, 70, 10));
    dl->AddCircleFilled(pg.P(cx + s * 0.09f, cy - s * 0.04f), pg.S(s * 0.035f), Col(140, 70, 10));
    dl->AddBezierCubic(pg.P(cx - s * 0.1f, cy + s * 0.08f), pg.P(cx - s * 0.04f, cy + s * 0.14f),
                       pg.P(cx + s * 0.04f, cy + s * 0.14f), pg.P(cx + s * 0.1f, cy + s * 0.08f), Col(140, 70, 10),
                       pg.S(1.5f));
}

void IconMoon(const Page& pg, float cx, float cy, float s, ImU32 bg) {
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.40f), Col(235, 235, 255));
    pg.dl->AddCircleFilled(pg.P(cx + s * 0.16f, cy - s * 0.10f), pg.S(s * 0.34f), bg);
}

void IconClock(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.42f), Col(250, 244, 230));
    dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.42f), Col(110, 50, 140), 0, pg.S(2.5f));
    dl->AddLine(pg.P(cx, cy), pg.P(cx, cy - s * 0.28f), Col(110, 50, 140), pg.S(2.2f));
    dl->AddLine(pg.P(cx, cy), pg.P(cx + s * 0.2f, cy + s * 0.06f), Col(110, 50, 140), pg.S(2.2f));
}

void IconPin(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddTriangleFilled(pg.P(cx - s * 0.26f, cy - s * 0.04f), pg.P(cx + s * 0.26f, cy - s * 0.04f),
                          pg.P(cx, cy + s * 0.45f), Col(130, 50, 170));
    dl->AddCircleFilled(pg.P(cx, cy - s * 0.14f), pg.S(s * 0.29f), Col(130, 50, 170));
    dl->AddCircleFilled(pg.P(cx, cy - s * 0.14f), pg.S(s * 0.11f), Col(250, 244, 230));
}

void IconChest(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.12f), pg.P(cx + s * 0.42f, cy + s * 0.32f), Col(150, 90, 40), pg.S(2));
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.34f), pg.P(cx + s * 0.42f, cy - s * 0.10f), Col(176, 110, 50),
                      pg.S(6), ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.13f), pg.P(cx + s * 0.42f, cy - s * 0.07f), cGold);
    dl->AddRectFilled(pg.P(cx - s * 0.08f, cy - s * 0.18f), pg.P(cx + s * 0.08f, cy + s * 0.04f), cGold, pg.S(2));
    dl->AddRect(pg.P(cx - s * 0.42f, cy - s * 0.34f), pg.P(cx + s * 0.42f, cy + s * 0.32f), Col(90, 50, 20), pg.S(3), 0,
                pg.S(1.5f));
}

void IconKey(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddCircle(pg.P(cx - s * 0.2f, cy), pg.S(s * 0.16f), cGold, 0, pg.S(s * 0.08f));
    dl->AddLine(pg.P(cx - s * 0.04f, cy), pg.P(cx + s * 0.4f, cy), cGold, pg.S(s * 0.09f));
    dl->AddLine(pg.P(cx + s * 0.26f, cy), pg.P(cx + s * 0.26f, cy + s * 0.14f), cGold, pg.S(s * 0.08f));
    dl->AddLine(pg.P(cx + s * 0.38f, cy), pg.P(cx + s * 0.38f, cy + s * 0.12f), cGold, pg.S(s * 0.08f));
}

void IconBottle(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.08f, cy - s * 0.42f), pg.P(cx + s * 0.08f, cy - s * 0.32f), Col(150, 100, 60), pg.S(2));
    dl->AddRectFilled(pg.P(cx - s * 0.1f, cy - s * 0.32f), pg.P(cx + s * 0.1f, cy - s * 0.14f), Col(210, 235, 250, 200));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.12f), pg.S(s * 0.28f), Col(210, 235, 250, 200));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.16f), pg.S(s * 0.2f), Col(250, 250, 245));
    dl->AddCircle(pg.P(cx, cy + s * 0.12f), pg.S(s * 0.28f), Col(120, 150, 170), 0, pg.S(1.5f));
}

void IconScroll(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.34f, cy - s * 0.26f), pg.P(cx + s * 0.34f, cy + s * 0.26f), Col(240, 220, 170));
    dl->AddCircleFilled(pg.P(cx - s * 0.34f, cy), pg.S(s * 0.1f), Col(200, 160, 100));
    dl->AddCircleFilled(pg.P(cx + s * 0.34f, cy), pg.S(s * 0.1f), Col(200, 160, 100));
    for (int i = 0; i < 3; i++) {
        dl->AddLine(pg.P(cx - s * 0.2f, cy - s * 0.12f + i * s * 0.12f), pg.P(cx + s * 0.2f, cy - s * 0.12f + i * s * 0.12f),
                    Col(150, 110, 60), pg.S(1.2f));
    }
}

void IconCouple(const Page& pg, float cx, float cy, float s, ImU32 col) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 2; i++) {
        float x = cx + (i == 0 ? -1 : 1) * s * 0.17f;
        float y = cy + (i == 0 ? -0.04f : 0.04f) * s;
        dl->AddCircleFilled(pg.P(x, y - s * 0.16f), pg.S(s * 0.14f), col);
        dl->AddEllipseFilled(pg.P(x, y + s * 0.22f), ImVec2(pg.S(s * 0.22f), pg.S(s * 0.2f)), col);
    }
}

void IconExclamation(const Page& pg, float cx, float cy, float s) {
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.38f), Col(250, 200, 60));
    pg.dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.38f), Col(160, 90, 20), 0, pg.S(1.5f));
    pg.dl->AddRectFilled(pg.P(cx - s * 0.05f, cy - s * 0.24f), pg.P(cx + s * 0.05f, cy + s * 0.06f), Col(120, 50, 10), pg.S(2));
    pg.dl->AddCircleFilled(pg.P(cx, cy + s * 0.17f), pg.S(s * 0.06f), Col(120, 50, 10));
}

void IconTower(const Page& pg, float x, float y, float s, ImU32 col) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(x - s * 0.12f, y - s * 0.5f), pg.P(x + s * 0.12f, y), col);
    dl->AddTriangleFilled(pg.P(x - s * 0.16f, y - s * 0.5f), pg.P(x, y - s * 0.78f), pg.P(x + s * 0.16f, y - s * 0.5f), col);
    dl->AddRectFilled(pg.P(x - s * 0.5f, y - s * 0.22f), pg.P(x - s * 0.18f, y), col);
    dl->AddTriangleFilled(pg.P(x - s * 0.54f, y - s * 0.22f), pg.P(x - s * 0.34f, y - s * 0.42f), pg.P(x - s * 0.14f, y - s * 0.22f), col);
    dl->AddRectFilled(pg.P(x + s * 0.18f, y - s * 0.3f), pg.P(x + s * 0.46f, y), col);
    dl->AddTriangleFilled(pg.P(x + s * 0.14f, y - s * 0.3f), pg.P(x + s * 0.32f, y - s * 0.5f), pg.P(x + s * 0.5f, y - s * 0.3f), col);
    dl->AddCircleFilled(pg.P(x, y - s * 0.34f), pg.S(s * 0.06f), Col(255, 220, 120));
}

enum class StepIcon { Envelope, Pendant, Sun, Mask, Heart, Key, Bottle, Book, Scroll, Couple, Exclamation };

StepIcon IconForText(const std::string& text, bool last) {
    auto has = [&](const char* word) { return text.find(word) != std::string::npos; };
    if (has("Letter") || has("Mail") || has("carta")) return StepIcon::Envelope;
    if (has("Pendant")) return StepIcon::Pendant;
    if (has("Sun")) return StepIcon::Sun;
    if (has("Piece of Heart")) return StepIcon::Heart;
    if (has("Mask") || has("Hat") || has("Hood")) return StepIcon::Mask;
    if (has("Key")) return StepIcon::Key;
    if (has("Milk") || has("garrafa") || has("Chateau")) return StepIcon::Bottle;
    if (has("Notebook")) return StepIcon::Book;
    if (has("código")) return StepIcon::Scroll;
    return last ? StepIcon::Couple : StepIcon::Exclamation;
}

// The game's own icon for a step, when it has one; it comes from the player's files, so an HD texture pack
// replaces it too.
const char* GameIconPath(StepIcon icon) {
    switch (icon) {
        case StepIcon::Envelope: return (const char*)gItemIcons[ITEM_LETTER_TO_KAFEI];
        case StepIcon::Pendant: return (const char*)gItemIcons[ITEM_PENDANT_OF_MEMORIES];
        case StepIcon::Heart: return (const char*)gItemIcons[ITEM_HEART_PIECE];
        case StepIcon::Key: return (const char*)gItemIcons[ITEM_ROOM_KEY];
        case StepIcon::Bottle: return (const char*)gItemIcons[ITEM_BOTTLE];
        case StepIcon::Book: return (const char*)gItemIcons[ITEM_BOMBERS_NOTEBOOK];
        case StepIcon::Scroll: return (const char*)gItemIcons[ITEM_DEED_LAND];
        default: return nullptr;
    }
}

bool GameIcon(const Page& pg, const char* path, float cx, float cy, float s) {
    ImTextureID id = Texture(path);
    if (id == nullptr) {
        return false;
    }
    pg.dl->AddImage(id, pg.P(cx - s / 2, cy - s / 2), pg.P(cx + s / 2, cy + s / 2));
    return true;
}

void DrawStepIcon(const Page& pg, StepIcon icon, float cx, float cy, float s, ImU32 tint = Col(120, 60, 160)) {
    if (GameIcon(pg, GameIconPath(icon), cx, cy, s * 0.95f)) {
        return;
    }
    switch (icon) {
        case StepIcon::Envelope: IconEnvelope(pg, cx, cy, s); break;
        case StepIcon::Pendant: IconPendant(pg, cx, cy, s); break;
        case StepIcon::Sun: IconSun(pg, cx, cy, s); break;
        case StepIcon::Mask: IconMaskFace(pg, cx, cy, s); break;
        case StepIcon::Heart: IconHeart(pg, cx, cy, s); break;
        case StepIcon::Key: IconKey(pg, cx, cy, s); break;
        case StepIcon::Bottle: IconBottle(pg, cx, cy, s); break;
        case StepIcon::Book: IconBook(pg, cx, cy, s); break;
        case StepIcon::Scroll: IconScroll(pg, cx, cy, s); break;
        case StepIcon::Couple: IconCouple(pg, cx, cy, s, tint); break;
        case StepIcon::Exclamation: IconExclamation(pg, cx, cy, s); break;
    }
}

void Tick(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    ImVec2 a = pg.P(cx - s * 0.36f, cy), b = pg.P(cx - s * 0.08f, cy + s * 0.3f), c = pg.P(cx + s * 0.42f, cy - s * 0.34f);
    dl->AddLine(a, b, Col(20, 70, 20), pg.S(s * 0.26f));
    dl->AddLine(b, c, Col(20, 70, 20), pg.S(s * 0.26f));
    dl->AddLine(a, b, cGreen, pg.S(s * 0.16f));
    dl->AddLine(b, c, cGreen, pg.S(s * 0.16f));
}

void CheckSquare(const Page& pg, float x, float y, float s, bool done) {
    pg.dl->AddRectFilled(pg.P(x, y), pg.P(x + s, y + s), Col(30, 14, 40, 200), pg.S(3));
    pg.dl->AddRect(pg.P(x, y), pg.P(x + s, y + s), Col(230, 220, 200), pg.S(3), 0, pg.S(2));
    if (done) {
        Tick(pg, x + s * 0.55f, y + s * 0.45f, s * 1.1f);
    }
}

void Picture(const Page& pg, const char* path, float x, float y, float s, float rounding, float alpha = 1.0f) {
    ImTextureID id = Texture(path);
    if (id == nullptr) {
        pg.dl->AddRectFilled(pg.P(x, y), pg.P(x + s, y + s), Col(90, 60, 110), pg.S(rounding));
        return;
    }
    pg.dl->AddImageRounded(id, pg.P(x, y), pg.P(x + s, y + s), ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, (int)(alpha * 255)), pg.S(rounding));
}

// An invisible button over a page rectangle; returns true when clicked.
bool Hit(const Page& pg, const char* id, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(pg.P(x, y));
    bool clicked = ImGui::InvisibleButton(id, ImVec2(pg.S(w), pg.S(h)));
    if (hovered) {
        *hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    }
    return clicked;
}

// ----- Data shared by the sections

struct ListRow {
    std::string name;
    bool known;
    bool done;
    const char* photo; // game texture, or nullptr for a drawn icon
    int drawnIcon;     // section icon used when there is no photo
};

const char* kSectionNames[SECTION_MAX] = { "Pessoas", "Máscaras", "Canções", "Fadas Perdidas" };
const char* kSectionHeaders[SECTION_MAX] = { "Pessoas de Termina", "Máscaras de Termina", "Canções",
                                             "Fadas Perdidas" };

void SectionIcon(const Page& pg, int section, float cx, float cy, float s) {
    const char* game = section == SECTION_PEOPLE  ? (const char*)gItemIcons[ITEM_BOMBERS_NOTEBOOK]
                       : section == SECTION_MASKS ? (const char*)gItemIcons[ITEM_MASK_DEKU]
                       : section == SECTION_SONGS ? (const char*)gItemIcons[ITEM_SONG_SONATA]
                                                  : nullptr;
    if (GameIcon(pg, game, cx, cy, s * 0.9f)) {
        return;
    }
    switch (section) {
        case SECTION_PEOPLE: IconBook(pg, cx, cy, s); break;
        case SECTION_MASKS: IconMaskFace(pg, cx, cy, s); break;
        case SECTION_SONGS: IconNote(pg, cx, cy, s); break;
        default: IconFairy(pg, cx, cy, s); break;
    }
}

std::vector<ListRow> BuildRows(int section) {
    std::vector<ListRow> rows;
    if (section == SECTION_PEOPLE) {
        for (s32 i = 0; i < BOMBERS_NOTEBOOK_PERSON_MAX; i++) {
            bool met = HasMetPerson(i);
            rows.push_back({ met ? sGuide[i].name : "???", met, met && PersonDone(i), PersonPhoto(i), section });
        }
        return rows;
    }
    int count = 0;
    const SimpleEntry* list = SimpleList(section, count);
    for (int i = 0; i < count; i++) {
        bool done = SimpleDone(section, list[i]);
        const char* photo = (section != SECTION_FAIRIES) ? SimpleIcon(section, list[i]) : nullptr;
        rows.push_back({ list[i].name, true, done, photo, section });
    }
    return rows;
}

int sPage[SECTION_MAX] = { 0, 0, 0, 0 };

// ----- Parts of the screen

void DrawBackdrop(const Page& pg) {
    ImDrawList* dl = pg.dl;
    Panel(pg, ImVec2(0, 0), ImVec2(kPageW, kPageH), Col(48, 20, 72), Col(20, 8, 32), 18);
    // Stone-like diagonal pattern on the outer frame
    for (float x = -kPageH; x < kPageW; x += 26) {
        dl->AddLine(pg.P(std::max(0.0f, x), std::max(0.0f, -x)), pg.P(std::min(kPageW, x + kPageH), std::min(kPageH, kPageW - x)),
                    Col(255, 255, 255, 7), pg.S(6));
    }
    GoldFrame(pg, ImVec2(6, 6), ImVec2(kPageW - 6, kPageH - 6), 18, 5);
    GoldFrame(pg, ImVec2(16, 92), ImVec2(kPageW - 16, kPageH - 62), 12, 3);
    CornerStuds(pg, ImVec2(6, 6), ImVec2(kPageW - 6, kPageH - 6), 18);
}

void DrawTitle(const Page& pg, int& section) {
    ImDrawList* dl = pg.dl;
    float cx = kPageW / 2;
    // Banner with angled ends
    ImVec2 pts[8] = { pg.P(cx - 270, 34), pg.P(cx + 270, 34), pg.P(cx + 300, 58), pg.P(cx + 270, 84),
                      pg.P(cx - 270, 84), pg.P(cx - 300, 58) };
    dl->AddConvexPolyFilled(pts, 6, Col(84, 30, 96));
    dl->AddRectFilledMultiColor(pg.P(cx - 268, 36), pg.P(cx + 268, 60), Col(120, 50, 130), Col(120, 50, 130),
                                Col(84, 30, 96), Col(84, 30, 96));
    dl->AddPolyline(pts, 6, Col(40, 15, 10), ImDrawFlags_Closed, pg.S(5));
    dl->AddPolyline(pts, 6, cGold, ImDrawFlags_Closed, pg.S(3));
    // Scroll curls
    for (int i = 0; i < 2; i++) {
        float side = i == 0 ? -1.0f : 1.0f;
        dl->AddCircle(pg.P(cx + side * 245, 59), pg.S(9), cGold, 0, pg.S(2));
        dl->AddCircle(pg.P(cx + side * 228, 59), pg.S(5), cGold, 0, pg.S(2));
    }
    IconMajora(pg, cx, 18, 52);
    TextCentered(pg, cx, 42, 34, Col(250, 234, 190), "Caderno dos Bombers");

    // L and R switch the section
    const char* tabs[2] = { "L", "R" };
    for (int i = 0; i < 2; i++) {
        float x = (i == 0) ? cx - 360 : cx + 318;
        bool hovered = false;
        if (Hit(pg, tabs[i], x, 40, 42, 38, &hovered)) {
            section = (section + (i == 0 ? SECTION_MAX - 1 : 1)) % SECTION_MAX;
        }
        dl->AddRectFilled(pg.P(x, 40), pg.P(x + 42, 78), hovered ? Col(150, 140, 170) : Col(110, 100, 130), pg.S(6));
        dl->AddRect(pg.P(x, 40), pg.P(x + 42, 78), Col(220, 215, 230), pg.S(6), 0, pg.S(2));
        TextCentered(pg, x + 21, 46, 26, Col(40, 30, 60), tabs[i], false);
    }
}

void DrawClock(const Page& pg) {
    if (gPlayState == nullptr) {
        return;
    }
    ImDrawList* dl = pg.dl;
    float x0 = 862, x1 = 1082, y0 = 0, y1 = 36;
    ImVec2 pts[6] = { pg.P(x0 + 14, y0), pg.P(x1, y0), pg.P(x1, y1), pg.P(x0 + 14, y1), pg.P(x0, (y0 + y1) / 2) };
    dl->AddConvexPolyFilled(pts, 5, Col(34, 16, 60));
    dl->AddPolyline(pts, 5, cGold, ImDrawFlags_Closed, pg.S(2.5f));
    s32 minutes = (s32)TIME_TO_MINUTES_F(CURRENT_TIME);
    bool night = minutes < 6 * 60 || minutes >= 18 * 60;
    float cy = (y0 + y1) / 2;
    IconSun(pg, x0 + 32, cy, 26);
    std::string day = fmt::format("Dia {}", CURRENT_DAY);
    TextShadow(pg, x0 + 50, cy - 12, 24, Col(250, 230, 150), day.c_str());
    if (night) {
        IconMoon(pg, x0 + 140, cy, 26, Col(34, 16, 60));
    } else {
        IconSun(pg, x0 + 140, cy, 20);
    }
    std::string clock = fmt::format("{:02d}:{:02d}", minutes / 60, minutes % 60);
    TextShadow(pg, x0 + 156, cy - 12, 24, Col(250, 250, 255), clock.c_str());
}

void DrawSidebar(const Page& pg, int& section) {
    for (int i = 0; i < SECTION_MAX; i++) {
        float x = 34, y = 112 + i * 76, w = 210, h = 64;
        bool hovered = false;
        if (Hit(pg, kSectionNames[i], x, y, w, h, &hovered)) {
            section = i;
        }
        bool active = section == i;
        Panel(pg, ImVec2(x, y), ImVec2(x + w, y + h), active ? Col(110, 60, 120) : (hovered ? Col(92, 50, 118) : Col(72, 36, 98)),
              active ? Col(70, 30, 80) : Col(46, 20, 66), 10);
        if (active) {
            GoldFrame(pg, ImVec2(x, y), ImVec2(x + w, y + h), 10, 3.5f, true);
        } else {
            pg.dl->AddRect(pg.P(x, y), pg.P(x + w, y + h), Col(150, 110, 170, 160), pg.S(10), 0, pg.S(1.5f));
        }
        SectionIcon(pg, i, x + 36, y + h / 2, 44);
        ImU32 textCol = active ? Col(255, 228, 140) : Col(240, 230, 250);
        const char* name = kSectionNames[i];
        const char* space = strchr(name, ' ');
        if (space != nullptr && ImGui::GetFont()->CalcTextSizeA(22, FLT_MAX, 0, name).x > w - 78) {
            std::string first(name, space);
            TextShadow(pg, x + 68, y + 7, 22, textCol, first.c_str());
            TextShadow(pg, x + 68, y + 32, 22, textCol, space + 1);
        } else {
            TextShadow(pg, x + 68, y + 19, 22, textCol, name);
        }
    }
}

void DrawList(const Page& pg, int section, const std::vector<ListRow>& rows, int& selected) {
    ImDrawList* dl = pg.dl;
    float x0 = 258, y0 = 104, x1 = 612, y1 = 630;
    Panel(pg, ImVec2(x0, y0), ImVec2(x1, y1), Col(66, 30, 92), Col(40, 16, 58), 12);
    GoldFrame(pg, ImVec2(x0, y0), ImVec2(x1, y1), 12, 3);
    // Header
    Gradient(pg, ImVec2(x0 + 8, y0 + 8), ImVec2(x1 - 8, y0 + 58), Col(110, 50, 120), Col(70, 28, 90));
    dl->AddLine(pg.P(x0 + 8, y0 + 58), pg.P(x1 - 8, y0 + 58), cGold, pg.S(2));
    IconTower(pg, x0 + 44, y0 + 52, 56, Col(220, 180, 110));
    TextShadow(pg, x0 + 84, y0 + 18, 25, Col(255, 236, 190), kSectionHeaders[section]);

    int pages = std::max(1, (int)((rows.size() + kRowsPerPage - 1) / kRowsPerPage));
    int& page = sPage[section];
    page = std::clamp(page, 0, pages - 1);
    for (int r = 0; r < kRowsPerPage; r++) {
        int i = page * kRowsPerPage + r;
        if (i >= (int)rows.size()) {
            break;
        }
        const ListRow& row = rows[i];
        float x = x0 + 14, y = y0 + 68 + r * 52, w = (x1 - x0) - 28, h = 46;
        bool hovered = false;
        ImGui::PushID(i);
        if (Hit(pg, "row", x, y, w, h, &hovered)) {
            selected = i;
        }
        ImGui::PopID();
        bool isSelected = selected == i;
        dl->AddRectFilled(pg.P(x, y), pg.P(x + w, y + h),
                          isSelected ? Col(110, 60, 70) : (hovered ? Col(82, 44, 104) : Col(56, 26, 76)), pg.S(8));
        if (isSelected) {
            GoldFrame(pg, ImVec2(x, y), ImVec2(x + w, y + h), 8, 3, true);
        } else {
            dl->AddLine(pg.P(x + 6, y + h + 3), pg.P(x + w - 6, y + h + 3), Col(140, 100, 150, 120), pg.S(1));
        }
        if (row.photo != nullptr) {
            float alpha = row.known ? (row.done || section == SECTION_PEOPLE ? 1.0f : 0.55f) : 0.25f;
            if (section == SECTION_PEOPLE) {
                dl->AddCircleFilled(pg.P(x + 30, y + h / 2), pg.S(21), Col(220, 180, 110));
                Picture(pg, row.photo, x + 11, y + 4, 38, 19, alpha);
            } else {
                Picture(pg, row.photo, x + 11, y + 4, 38, 0, alpha);
            }
        } else {
            SectionIcon(pg, section, x + 30, y + h / 2, 40);
        }
        // Long names shrink to stay clear of the check box
        float nameSize = 21, room = w - 64 - 52;
        float nameW = ImGui::GetFont()->CalcTextSizeA(nameSize, FLT_MAX, 0, row.name.c_str()).x;
        if (nameW > room) {
            nameSize = std::max(15.0f, nameSize * room / nameW);
        }
        TextShadow(pg, x + 64, y + 23 - nameSize / 2, nameSize, row.known ? Col(250, 240, 230) : Col(170, 150, 180),
                   row.name.c_str());
        CheckSquare(pg, x + w - 40, y + 10, 26, row.done);
    }
    // Pages
    if (pages > 1) {
        std::string label = fmt::format("{} / {}", page + 1, pages);
        float cy = y1 - 26;
        TextCentered(pg, (x0 + x1) / 2, cy - 12, 22, Col(250, 240, 220), label.c_str());
        if (Hit(pg, "prev", (x0 + x1) / 2 - 90, cy - 16, 36, 32)) {
            page = (page + pages - 1) % pages;
        }
        if (Hit(pg, "next", (x0 + x1) / 2 + 54, cy - 16, 36, 32)) {
            page = (page + 1) % pages;
        }
        dl->AddTriangleFilled(pg.P((x0 + x1) / 2 - 62, cy - 10), pg.P((x0 + x1) / 2 - 62, cy + 10),
                              pg.P((x0 + x1) / 2 - 80, cy), Col(250, 240, 220));
        dl->AddTriangleFilled(pg.P((x0 + x1) / 2 + 62, cy - 10), pg.P((x0 + x1) / 2 + 80, cy),
                              pg.P((x0 + x1) / 2 + 62, cy + 10), Col(250, 240, 220));
    }
}

struct DetailTile {
    StepIcon icon;
    bool done;
    std::string tooltip;
};

void DrawDetailFrame(const Page& pg, float x0, float y0, float x1, float y1) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(x0 + 4, y0 + 6), pg.P(x1 + 4, y1 + 6), Col(0, 0, 0, 90), pg.S(10));
    Panel(pg, ImVec2(x0, y0), ImVec2(x1, y1), cParchment, cParchmentDark, 10);
    dl->AddRect(pg.P(x0, y0), pg.P(x1, y1), Col(120, 80, 40), pg.S(10), 0, pg.S(3));
    dl->AddRect(pg.P(x0 + 8, y0 + 8), pg.P(x1 - 8, y1 - 8), Col(170, 130, 80, 150), pg.S(6), 0, pg.S(1.2f));
    // Burnt edges
    for (int i = 0; i < 4; i++) {
        dl->AddRect(pg.P(x0 + i, y0 + i), pg.P(x1 - i, y1 - i), Col(140, 100, 50, 40 - i * 8), pg.S(10), 0, pg.S(2));
    }
}

void Heading(const Page& pg, float x, float y, const char* text, int icon) {
    switch (icon) {
        case 0: IconTriforce(pg, x + 12, y + 13, 24, Col(210, 160, 40)); break;
        case 1: IconClock(pg, x + 12, y + 13, 26); break;
        case 2: IconPin(pg, x + 12, y + 13, 28); break;
        case 3: IconChest(pg, x + 13, y + 13, 28); break;
    }
    Text(pg, x + 32, y, 24, cWine, text);
}

void DrawDetail(const Page& pg, const char* title, const char* summary, const char* photo, int sectionIcon,
                const std::vector<DetailTile>& tiles, const std::string& when, const std::string& where,
                const std::string& reward, StepIcon rewardIcon, const char* rewardPicture, const std::string& next,
                bool known) {
    ImDrawList* dl = pg.dl;
    float x0 = 628, y0 = 104, x1 = 1072, y1 = 630;
    DrawDetailFrame(pg, x0, y0, x1, y1);

    if (!known) {
        IconMajora(pg, (x0 + x1) / 2, 300, 120);
        TextCentered(pg, (x0 + x1) / 2, 390, 30, cWine, "???", false);
        Text(pg, x0 + 40, 440, 19, cInk, "Você ainda não conheceu esta pessoa. O Caderno registra cada um quando "
                                         "vocês se encontram.", x1 - x0 - 80);
        return;
    }

    // Title and picture
    IconMajora(pg, x0 + 40, y0 + 42, 46);
    Text(pg, x0 + 72, y0 + 20, 32, cWine, title, 210);
    float titleH = WrappedHeight(pg, 32, 210, title);
    dl->AddLine(pg.P(x0 + 24, y0 + 30 + titleH), pg.P(x0 + 290, y0 + 30 + titleH), Col(150, 100, 60), pg.S(1.5f));
    float px = x1 - 152, py = y0 + 18;
    dl->AddRectFilled(pg.P(px - 4, py - 4), pg.P(px + 134, py + 134), Col(160, 120, 70, 120), pg.S(12));
    if (photo != nullptr) {
        Picture(pg, photo, px, py, 130, 10);
    } else {
        dl->AddRectFilled(pg.P(px, py), pg.P(px + 130, py + 130), Col(225, 205, 165), pg.S(10));
        SectionIcon(pg, sectionIcon, px + 65, py + 65, 96);
    }
    dl->AddRect(pg.P(px, py), pg.P(px + 130, py + 130), Col(130, 90, 50), pg.S(10), 0, pg.S(2));
    Text(pg, x0 + 26, y0 + 44 + titleH, 18, cInk, summary, 248);

    // Progress tiles
    float ty = y0 + 172;
    dl->AddLine(pg.P(x0 + 22, ty - 8), pg.P(x1 - 22, ty - 8), Col(170, 130, 80, 160), pg.S(1));
    // A single-step entry has nothing to track; its status is already in the summary, so the space goes to the
    // place and the reward.
    bool showTiles = tiles.size() > 1;
    if (showTiles) {
        Heading(pg, x0 + 24, ty, "Progresso da missão", 0);
    }
    int nextIndex = -1;
    for (int i = 0; i < (int)tiles.size(); i++) {
        if (!tiles[i].done) {
            nextIndex = i;
            break;
        }
    }
    int shown = showTiles ? std::min<int>(tiles.size(), 5) : 0;
    float gap = 12, room = (x1 - x0) - 56;
    float tw = std::min(84.0f, (room - gap * (shown - 1)) / std::max(shown, 1)), th = 62;
    float startX = (x0 + x1) / 2 - (shown * tw + (shown - 1) * gap) / 2;
    for (int i = 0; i < shown; i++) {
        float x = startX + i * (tw + gap), y = ty + 36;
        bool isNext = i == nextIndex;
        dl->AddRectFilled(pg.P(x, y), pg.P(x + tw, y + th), isNext ? Col(70, 30, 90) : Col(196, 170, 120), pg.S(8));
        if (isNext) {
            GoldFrame(pg, ImVec2(x, y), ImVec2(x + tw, y + th), 8, 2.5f, true);
        } else {
            dl->AddRect(pg.P(x, y), pg.P(x + tw, y + th), Col(120, 80, 40), pg.S(8), 0, pg.S(2));
        }
        DrawStepIcon(pg, tiles[i].icon, x + tw / 2, y + th / 2, 46, isNext ? Col(220, 170, 255) : Col(110, 60, 140));
        if (tiles[i].done) {
            Tick(pg, x + tw - 14, y + th - 14, 26);
        }
        bool hovered = false;
        ImGui::PushID(i);
        Hit(pg, "tile", x, y, tw, th, &hovered);
        ImGui::PopID();
        if (hovered && !tiles[i].tooltip.empty()) {
            ImGui::SetTooltip("%s", tiles[i].tooltip.c_str());
        }
    }

    // When and where
    float wy = showTiles ? ty + 116 : ty;
    if (showTiles) {
        dl->AddLine(pg.P(x0 + 22, wy - 8), pg.P(x1 - 22, wy - 8), Col(170, 130, 80, 160), pg.S(1));
    }
    float midX = (x0 + x1) / 2;
    float textH = std::max(WrappedHeight(pg, 18, midX - x0 - 70, when.c_str()),
                           WrappedHeight(pg, 18, x1 - midX - 64, where.c_str()));
    Heading(pg, x0 + 24, wy, "Horário", 1);
    Text(pg, x0 + 56, wy + 30, 18, cInk, when.c_str(), midX - x0 - 70);
    dl->AddLine(pg.P(midX, wy), pg.P(midX, wy + 80 + std::max(0.0f, textH - 40)), Col(170, 130, 80, 160), pg.S(1));
    Heading(pg, midX + 12, wy, "Local", 2);
    Text(pg, midX + 44, wy + 30, 18, cInk, where.c_str(), x1 - midX - 64);

    // Reward and next step
    float ry = wy + std::max(100.0f, textH + 50);
    dl->AddLine(pg.P(x0 + 22, ry - 8), pg.P(x1 - 22, ry - 8), Col(170, 130, 80, 160), pg.S(1));
    Heading(pg, x0 + 24, ry, "Recompensa", 3);
    if (!reward.empty()) {
        dl->AddCircleFilled(pg.P(x0 + 60, ry + 70), pg.S(30), Col(200, 170, 120));
        dl->AddCircle(pg.P(x0 + 60, ry + 70), pg.S(30), Col(130, 90, 50), 0, pg.S(2));
        if (rewardPicture != nullptr) {
            Picture(pg, rewardPicture, x0 + 36, ry + 46, 48, 6);
        } else {
            DrawStepIcon(pg, rewardIcon, x0 + 60, ry + 70, 42);
        }
        Text(pg, x0 + 100, ry + 44, 18, cInk, reward.c_str(), midX - x0 - 90);
    }
    if (!next.empty()) {
        float nx0 = midX + 8, ny0 = ry, nx1 = x1 - 20, ny1 = y1 - 22;
        dl->AddRectFilled(pg.P(nx0, ny0), pg.P(nx1, ny1), Col(46, 22, 70), pg.S(8));
        GoldFrame(pg, ImVec2(nx0, ny0), ImVec2(nx1, ny1), 8, 2.5f);
        Text(pg, nx0 + 12, ny0 + 10, 19, Col(255, 226, 140), "Próxima etapa");
        Text(pg, nx0 + 12, ny0 + 36, 16, Col(240, 230, 250), next.c_str(), nx1 - nx0 - 24);
    }
}

void DrawFooter(const Page& pg) {
    ImDrawList* dl = pg.dl;
    float cx = kPageW / 2, y0 = kPageH - 54, y1 = kPageH - 12;
    ImVec2 pts[6] = { pg.P(cx - 330, y0), pg.P(cx + 330, y0), pg.P(cx + 356, (y0 + y1) / 2), pg.P(cx + 330, y1),
                      pg.P(cx - 330, y1), pg.P(cx - 356, (y0 + y1) / 2) };
    dl->AddConvexPolyFilled(pts, 6, Col(64, 26, 84));
    dl->AddPolyline(pts, 6, cGold, ImDrawFlags_Closed, pg.S(2.5f));
    IconTriforce(pg, cx - 300, (y0 + y1) / 2, 30);
    IconTriforce(pg, cx + 300, (y0 + y1) / 2, 30);
    TextCentered(pg, cx, y0 + 2, 18, Col(250, 240, 220), "Acompanhe as pessoas e os eventos de Termina");
    TextCentered(pg, cx, y0 + 21, 18, Col(250, 240, 220), "no Caderno dos Bombers.");
}

std::string WhenForPerson(const GuidePerson& person) {
    u8 days = 0;
    for (const GuideStep& step : person.steps) {
        days |= step.days;
    }
    std::string text;
    if (days == ANY_DAY) {
        text = "Dia 1 a Dia 3";
    } else {
        text = DaysText(days);
    }
    for (const GuideStep& step : person.steps) {
        if (!IsStepDone(step)) {
            return text + "\n(" + step.time + ")";
        }
    }
    return text;
}

std::string StripReward(const char* task) {
    std::string text = task;
    size_t pos = text.find(". Recompensa: ");
    if (pos != std::string::npos) {
        text.resize(pos + 1);
    }
    return text;
}

void DrawPersonPage(const Page& pg, s32 person) {
    if (!HasMetPerson(person)) {
        DrawDetail(pg, "", "", nullptr, SECTION_PEOPLE, {}, "", "", "", StepIcon::Exclamation, nullptr, "", false);
        return;
    }
    const GuidePerson& guide = sGuide[person];
    std::vector<DetailTile> tiles;
    std::string next;
    for (size_t i = 0; i < guide.steps.size(); i++) {
        const GuideStep& step = guide.steps[i];
        bool done = IsStepDone(step);
        std::string reward = RewardOf(step.task);
        tiles.push_back({ IconForText(reward.empty() ? step.task : reward, i + 1 == guide.steps.size()), done,
                          DaysText(step.days) + " - " + step.time + "\n" + StripReward(step.task) });
        if (!done && next.empty()) {
            next = DaysText(step.days) + ", " + step.time + ": " + StripReward(step.task);
        }
    }
    std::string reward;
    for (auto it = guide.steps.rbegin(); it != guide.steps.rend() && reward.empty(); ++it) {
        reward = RewardOf(it->task);
    }
    DrawDetail(pg, guide.name, sPeopleInfo[person].summary, PersonPhoto(person), SECTION_PEOPLE, tiles,
               WhenForPerson(guide), sPeopleInfo[person].place, reward, IconForText(reward, false), nullptr,
               next.empty() ? "Tudo feito neste ciclo!" : next, true);
}

void DrawSimplePage(const Page& pg, int section, const SimpleEntry& entry) {
    bool done = SimpleDone(section, entry);
    std::vector<DetailTile> tiles = { { section == SECTION_SONGS ? StepIcon::Scroll
                                        : section == SECTION_FAIRIES ? StepIcon::Exclamation
                                                                     : StepIcon::Mask,
                                        done, done ? "Já conseguiu" : "Ainda não conseguiu" } };
    std::string status;
    if (section == SECTION_FAIRIES && entry.id >= 0 && gPlayState != nullptr) {
        status = fmt::format("{} de 15 fadas encontradas.", (int)gSaveContext.save.saveInfo.inventory.strayFairies[entry.id]);
    } else {
        status = done ? "Você já conseguiu." : "Você ainda não conseguiu.";
    }
    const char* rewardText = section == SECTION_SONGS ? "A canção na ocarina"
                             : section == SECTION_FAIRIES ? (entry.id < 0 ? "Máscara da Grande Fada" : "Poder da Grande Fada")
                                                          : entry.name;
    const char* picture = section != SECTION_FAIRIES ? SimpleIcon(section, entry) : nullptr;
    const char* rewardPicture = (section != SECTION_FAIRIES || entry.id < 0) ? SimpleIcon(section, entry) : nullptr;
    DrawDetail(pg, entry.name, status.c_str(), picture, section, tiles, entry.when, entry.where, rewardText,
               section == SECTION_SONGS ? StepIcon::Scroll : StepIcon::Mask, rewardPicture, done ? "" : entry.where,
               true);
}

} // namespace

void BombersNotebookGuideWindow::DrawElement() {
    ImGui::SetNextWindowSize(ImVec2(1100, 740), ImGuiCond_FirstUseEver);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float k = std::max(0.3f, std::min(avail.x / kPageW, (avail.y - 30) / kPageH));
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += std::max(0.0f, (avail.x - kPageW * k) / 2);
    Page pg{ ImGui::GetWindowDrawList(), origin, k };

    DrawBackdrop(pg);
    DrawTitle(pg, sSection);
    DrawClock(pg);
    DrawSidebar(pg, sSection);
    std::vector<ListRow> rows = BuildRows(sSection);
    int& selected = sSelected[sSection];
    selected = std::clamp(selected, 0, std::max(0, (int)rows.size() - 1));
    DrawList(pg, sSection, rows, selected);
    if (sSection == SECTION_PEOPLE) {
        DrawPersonPage(pg, selected);
    } else {
        int count = 0;
        const SimpleEntry* list = SimpleList(sSection, count);
        if (count > 0) {
            DrawSimplePage(pg, sSection, list[selected]);
        }
    }
    DrawFooter(pg);
    if (gPlayState == nullptr) {
        TextCentered(pg, kPageW / 2, kPageH - 90, 18, Col(255, 220, 160), "Carregue um save para ver o seu progresso.");
    }

    // Space for the page, then the option below it.
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, origin.y + kPageH * k + 6));
    UIWidgets::CVarCheckbox("Mostrar dicas dentro do Caderno do jogo", CVAR_NAME,
                            UIWidgets::CheckboxOptions().DefaultValue(true));
}
