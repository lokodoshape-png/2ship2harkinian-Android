#include "BombersNotebookGuide.h"
#include "MM3D.h"
#include "2s2h/BenGui/UIWidgets.hpp"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>
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

typedef struct {
    const char* reward;
    const char* when;
    const char* where;
    const char* needs;
    u16 doneFlag; // Notebook flag set once the reward is received, 0 when there is none
} GuideReward;

typedef struct {
    const char* name;
    std::vector<GuideReward> rewards;
} GuidePerson;

// Indexed by BombersNotebookPerson, in the same order as the notebook rows
static const GuidePerson sGuide[BOMBERS_NOTEBOOK_PERSON_MAX] = {
    { "Bombers (Jim)",
      { { "Bombers' Notebook", "Dias 1 a 3, durante o dia", "North Clock Town",
          "Forma humana: estoure o balão que o Jim tenta pegar", NOTEBOOK_FLAG(RECEIVED_BOMBERS_NOTEBOOK) },
        { "Código secreto (passa pelo guarda do observatório)", "Dias 1 a 3, durante o dia", "Clock Town inteira",
          "Forma Deku: estoure o balão e ache os 5 Bombers no esconde-esconde",
          NOTEBOOK_FLAG(LEARNED_SECRET_CODE) } } },
    { "Anju",
      { { "Room Key (chave do quarto)", "Dia 1, à tarde, antes do Goron chegar", "Balcão da Stock Pot Inn",
          "Diga que tem uma reserva", NOTEBOOK_FLAG(RECEIVED_ROOM_KEY) },
        { "Letter to Kafei", "Dia 1, encontro na cozinha às 23:30", "Stock Pot Inn",
          "Use a Kafei's Mask e fale com a Anju no balcão para marcar o encontro",
          NOTEBOOK_FLAG(RECEIVED_LETTER_TO_KAFEI) },
        { "Couple's Mask", "Dia 3, entre 4:00 e 6:00 (amanhecer final)", "Quarto da Anju, Stock Pot Inn",
          "Entregar o Pendant of Memories e ajudar o Kafei no Sakon's Hideout",
          NOTEBOOK_FLAG(RECEIVED_COUPLES_MASK) } } },
    { "Kafei",
      { { "Pendant of Memories", "Dia 2, à tarde (depois que o carteiro entrega a carta)",
          "Sala dos fundos da Curiosity Shop, Laundry Pool", "Ter colocado a Letter to Kafei numa caixa de correio",
          NOTEBOOK_FLAG(RECEIVED_PENDANT_OF_MEMORIES) },
        { "Ajuda no Sakon's Hideout (necessário para a Couple's Mask)", "Dia 3, por volta das 18:00",
          "Ikana Canyon, entrada do Sakon's Hideout", "Ter falado com o Kafei no Dia 2",
          NOTEBOOK_FLAG(ESCAPED_SAKONS_HIDEOUT) } } },
    { "Curiosity Shop Man",
      { { "Keaton Mask + Priority Mail", "Dia 3, à tarde/noite (depois que o Kafei sai)",
          "Sala dos fundos da Curiosity Shop, Laundry Pool", "Ter encontrado o Kafei no Dia 2",
          NOTEBOOK_FLAG(RECEIVED_KEATON_MASK) },
        { "All-Night Mask (500 rúpias)", "Dia 3, à noite", "Curiosity Shop, West Clock Town",
          "Ter salvo a Bomb Shop Lady do Sakon no Dia 1 e ter a Giant's Wallet",
          NOTEBOOK_FLAG(RECEIVED_ALL_NIGHT_MASK) } } },
    { "Bomb Shop Lady",
      { { "Blast Mask", "Dia 1, meia-noite (0:00)", "North Clock Town", "Atacar o Sakon quando ele rouba a bolsa dela",
          NOTEBOOK_FLAG(RECEIVED_BLAST_MASK) } } },
    { "Romani",
      { { "Bottle with Milk (garrafa)", "Noite do Dia 1, de 2:30 até 5:15", "Romani Ranch",
          "Falar com a Romani no Dia 1 antes das 18:00 e defender o celeiro com o arco; Powder Keg para abrir a "
          "Milk Road",
          NOTEBOOK_FLAG(RECEIVED_ALIENS_BOTTLE) } } },
    { "Cremia",
      { { "Romani's Mask", "Dia 2, às 18:00", "Celeiro do Romani Ranch",
          "Ter defendido o rancho na noite do Dia 1; proteger a carroça com o arco",
          NOTEBOOK_FLAG(RECEIVED_ROMANIS_MASK) } } },
    { "Mayor Dotour",
      { { "Piece of Heart", "Dias 1 a 3, durante a reunião (dia)", "Gabinete do prefeito, East Clock Town",
          "Usar a Couple's Mask na reunião", NOTEBOOK_FLAG(RECEIVED_MAYOR_HP) } } },
    { "Madame Aroma",
      { { "Kafei's Mask", "Dias 1 ou 2, durante o dia", "Mayor's Residence, East Clock Town", "Só falar com ela",
          NOTEBOOK_FLAG(RECEIVED_KAFEIS_MASK) },
        { "Chateau Romani", "Dia 3, depois das 18:00", "Milk Bar, East Clock Town",
          "Entregar a Priority Mail; precisa da Romani's Mask para entrar",
          NOTEBOOK_FLAG(DELIVERED_PRIORITY_MAIL) } } },
    { "Toto",
      { { "Circus Leader's Mask (com o Gorman)", "Noites dos Dias 1 ou 2", "Milk Bar, East Clock Town",
          "Romani's Mask para entrar; tocar no palco com as máscaras Deku, Goron e Zora",
          NOTEBOOK_FLAG(RECEIVED_CIRCUS_LEADERS_MASK) } } },
    { "Gorman",
      { { "Circus Leader's Mask", "Noites dos Dias 1 ou 2", "Milk Bar, East Clock Town",
          "Fazer a apresentação do Toto (máscaras Deku, Goron e Zora)",
          NOTEBOOK_FLAG(RECEIVED_CIRCUS_LEADERS_MASK) } } },
    { "Postman",
      { { "Piece of Heart (jogo dos 10 segundos)", "Dias 1 ou 2, à noite", "Post Office, West Clock Town",
          "Parar o relógio em 10,00 s; a Bunny Hood mostra o tempo", NOTEBOOK_FLAG(RECEIVED_POSTMAN_HP) },
        { "Postman's Hat", "Dia 3, à noite", "Post Office, West Clock Town", "Entregar a Priority Mail para ele",
          NOTEBOOK_FLAG(RECEIVED_POSTMANS_HAT) } } },
    { "Rosa Sisters",
      { { "Piece of Heart", "Dias 1 ou 2, à noite", "West Clock Town", "Usar a Kamaro's Mask e dançar para elas",
          NOTEBOOK_FLAG(RECEIVED_ROSA_SISTERS_HP) } } },
    { "Toilet Hand",
      { { "Piece of Heart", "Qualquer dia, entre 0:00 e 6:00", "Banheiro da Stock Pot Inn",
          "Dar um papel: um Title Deed ou uma carta", NOTEBOOK_FLAG(RECEIVED_TOILET_HAND_HP) } } },
    { "Avó da Anju",
      { { "Piece of Heart (história curta)", "Dias 1 ou 2", "Stock Pot Inn",
          "Usar a All-Night Mask para não dormir e acertar a pergunta",
          NOTEBOOK_FLAG(RECEIVED_GRANDMA_SHORT_STORY_HP) },
        { "Piece of Heart (história longa)", "Dias 1 ou 2", "Stock Pot Inn",
          "Usar a All-Night Mask para não dormir e acertar a pergunta",
          NOTEBOOK_FLAG(RECEIVED_GRANDMA_LONG_STORY_HP) } } },
    { "Kamaro",
      { { "Kamaro's Mask", "Qualquer dia, entre 0:00 e 6:00", "Termina Field, norte", "Tocar a Song of Healing",
          NOTEBOOK_FLAG(RECEIVED_KAMAROS_MASK) } } },
    { "Grog",
      { { "Bunny Hood", "Dias 1 a 3, durante o dia", "Cucco Shack, Romani Ranch",
          "Usar a Bremen Mask e marchar até os pintinhos virarem galinhas", NOTEBOOK_FLAG(RECEIVED_BUNNY_HOOD) } } },
    { "Gorman Brothers",
      { { "Garo's Mask", "Dias 1 ou 2, durante o dia", "Gorman Track, Milk Road", "Ganhar a corrida com a Epona",
          NOTEBOOK_FLAG(RECEIVED_GAROS_MASK) } } },
    { "Shiro",
      { { "Stone Mask", "Qualquer dia", "Road to Ikana", "Lens of Truth para vê-lo; dar uma Red Potion",
          NOTEBOOK_FLAG(RECEIVED_STONE_MASK) } } },
    { "Guru-Guru",
      { { "Bremen Mask", "Dias 1 ou 2, à noite (18:00 até 6:00)", "Laundry Pool", "Só ouvir a história dele",
          NOTEBOOK_FLAG(RECEIVED_BREMEN_MASK) } } },
};

static bool IsRewardDone(const GuideReward& reward) {
    return (reward.doneFlag != 0) && (gPlayState != nullptr) && CHECK_WEEKEVENTREG(reward.doneFlag);
}

static s32 GetSelectedPerson() {
    if (gPlayState == nullptr || !sBombersNotebookOpen ||
        sBombersNotebook.loadState != BOMBERS_NOTEBOOK_LOAD_STATE_DONE) {
        return -1;
    }
    s32 person = sBombersNotebook.cursorPageRow + sBombersNotebook.cursorPage;
    return (person >= 0 && person < BOMBERS_NOTEBOOK_PERSON_MAX) ? person : -1;
}

static void DrawPerson(const GuidePerson& person, bool highlighted) {
    s32 done = 0;
    for (const GuideReward& reward : person.rewards) {
        done += IsRewardDone(reward);
    }

    ImGui::PushID(person.name);
    if (highlighted) {
        ImGui::SetNextItemOpen(true);
    }
    std::string header = fmt::format("{} ({}/{})", person.name, done, person.rewards.size());
    ImGui::PushStyleColor(ImGuiCol_Text, highlighted                       ? ImVec4(1.0f, 0.9f, 0.3f, 1.0f)
                                         : (done == person.rewards.size()) ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                                                                           : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    bool open = ImGui::CollapsingHeader(header.c_str());
    ImGui::PopStyleColor();

    if (open) {
        for (const GuideReward& reward : person.rewards) {
            ImGui::Bullet();
            ImGui::TextColored(IsRewardDone(reward) ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                               "%s%s", reward.reward, IsRewardDone(reward) ? "  (recebido)" : "");
            ImGui::Indent();
            ImGui::TextWrapped("Quando: %s", reward.when);
            ImGui::TextWrapped("Onde: %s", reward.where);
            ImGui::TextWrapped("Precisa: %s", reward.needs);
            ImGui::Unindent();
        }
    }
    ImGui::PopID();
}

void BombersNotebookGuideWindow::InitElement() {
}

void BombersNotebookGuideWindow::UpdateElement() {
    if (!CVarGetInteger(CVAR_NAME, 0)) {
        return;
    }

    bool notebookOpen = (gPlayState != nullptr) && sBombersNotebookOpen;
    if (notebookOpen && !IsVisible()) {
        mAutoShown = true;
        SetVisibility(true);
    } else if (!notebookOpen && mAutoShown) {
        mAutoShown = false;
        SetVisibility(false);
    }
}

void BombersNotebookGuideWindow::DrawElement() {
    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);

    if (!ImGui::BeginChild("Bombers Notebook Guide")) {
        ImGui::EndChild();
        return;
    }

    UIWidgets::CVarCheckbox("Abrir sozinho junto com o Caderno", CVAR_NAME);
    ImGui::TextWrapped("Quem está selecionado no Caderno aparece em amarelo. Recompensas já registradas no "
                       "Caderno ficam em verde.");
    ImGui::Separator();

    s32 selected = GetSelectedPerson();
    if (selected >= 0) {
        DrawPerson(sGuide[selected], true);
        ImGui::Separator();
    }
    for (s32 i = 0; i < BOMBERS_NOTEBOOK_PERSON_MAX; i++) {
        if (i != selected) {
            DrawPerson(sGuide[i], false);
        }
    }

    ImGui::EndChild();
}
