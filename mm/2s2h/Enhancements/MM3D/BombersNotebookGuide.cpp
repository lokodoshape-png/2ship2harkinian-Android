#include "BombersNotebookGuide.h"
#include "MM3D.h"
#include "2s2h/BenGui/UIWidgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/fmt/fmt.h>
#include <algorithm>
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

void BombersNotebookGuideWindow::DrawElement() {
    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);

    if (!ImGui::BeginChild("Bombers Notebook Guide")) {
        ImGui::EndChild();
        return;
    }

    UIWidgets::CVarCheckbox("Mostrar dicas dentro do Caderno", CVAR_NAME, UIWidgets::CheckboxOptions().DefaultValue(true));
    ImGui::TextWrapped("As dicas de cada personagem aparecem depois que você o conhece, separadas por dia. Amarelo "
                       "é o que falta fazer; verde é o que o Caderno já registrou.");
    ImGui::Separator();

    s32 selected = GetSelectedPerson();
    for (s32 i = 0; i < BOMBERS_NOTEBOOK_PERSON_MAX; i++) {
        const GuidePerson& person = sGuide[i];
        ImGui::PushID(i);
        if (!HasMetPerson(i)) {
            ImGui::TextDisabled("???  (ainda não conheceu)");
            ImGui::PopID();
            continue;
        }
        s32 done = 0;
        for (const GuideStep& step : person.steps) {
            done += IsStepDone(step);
        }
        if (i == selected) {
            ImGui::SetNextItemOpen(true);
        }
        std::string header = fmt::format("{} ({}/{})", person.name, done, person.steps.size());
        ImGui::PushStyleColor(ImGuiCol_Text, (i == selected)                    ? sTodayText
                                             : (done == person.steps.size()) ? sDoneText
                                                                             : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        bool open = ImGui::CollapsingHeader(header.c_str());
        ImGui::PopStyleColor();
        if (open) {
            DrawPersonDays(person);
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
}
