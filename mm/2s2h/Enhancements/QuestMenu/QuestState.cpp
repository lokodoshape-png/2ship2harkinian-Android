#include "QuestState.h"

#include <algorithm>

extern "C" {
#include <z64.h>
#include "variables.h"
extern PlayState* gPlayState;
}

namespace QuestMenu {

bool SaveLoaded() {
    return gPlayState != nullptr && gSaveContext.fileNum != 0xFF;
}

QuestProgress Evaluate(const Quest& quest) {
    QuestProgress p;
    p.objectiveDone.assign(quest.objectives.size(), false);
    if (!SaveLoaded() || (quest.unlocked && !quest.unlocked())) {
        p.status = Status::Locked;
        for (const Objective& o : quest.objectives) {
            p.total += o.done ? 1 : 0;
        }
        return p;
    }

    bool anyDone = false;
    for (size_t i = 0; i < quest.objectives.size(); i++) {
        const Objective& o = quest.objectives[i];
        if (!o.done) {
            continue;
        }
        p.total++;
        if (o.done()) {
            p.done++;
            p.objectiveDone[i] = true;
            anyDone = true;
        }
    }
    bool counterFull = true;
    if (quest.counter.value) {
        p.counterValue = std::max(0, quest.counter.value());
        counterFull = p.counterValue >= quest.counter.max;
        anyDone = anyDone || p.counterValue > 0;
        p.total++;
        p.done += counterFull ? 1 : 0;
    }

    bool complete;
    if (quest.complete) {
        complete = quest.complete();
    } else {
        // A quest made only of steps the game does not record can't be judged here; it stays available.
        complete = p.total > 0 && p.done == p.total && counterFull;
    }

    if (complete) {
        p.status = Status::Done;
        std::fill(p.objectiveDone.begin(), p.objectiveDone.end(), true);
        p.done = p.total;
    } else if (anyDone) {
        p.status = Status::InProgress;
    } else {
        p.status = Status::Available;
    }
    return p;
}

const char* StatusName(Status status) {
    switch (status) {
        case Status::Locked: return "Bloqueada";
        case Status::Available: return "Disponível";
        case Status::InProgress: return "Em andamento";
        case Status::Done: return "Concluída";
    }
    return "";
}

const char* StatusAsset(Status status) {
    switch (status) {
        case Status::Locked: return "state_locked";
        case Status::Available: return "state_available";
        case Status::InProgress: return "state_progress";
        case Status::Done: return "state_done";
    }
    return "";
}

} // namespace QuestMenu
