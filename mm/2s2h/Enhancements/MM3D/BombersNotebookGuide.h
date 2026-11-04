#ifndef MM3D_BOMBERS_NOTEBOOK_GUIDE_H
#define MM3D_BOMBERS_NOTEBOOK_GUIDE_H

#include <ship/window/gui/GuiWindow.h>
#include <cstdint>
#include <vector>

// One step of a person's story, as listed in the guide.
struct GuideStep {
    uint8_t days;         // DAY_1 | DAY_2 | DAY_3 bits (1 << day)
    const char* time;
    const char* task;     // ends with "Recompensa: ..." when the step gives something
    uint16_t doneFlag;    // notebook flag set once this step is recorded, 0 when there is none
};

struct GuidePerson {
    const char* name;
    std::vector<GuideStep> steps;
};

// The guide's data, indexed by BombersNotebookPerson; shared with the quest menu.
const GuidePerson& BombersGuide_Person(int32_t person);
bool BombersGuide_IsStepDone(const GuideStep& step);
bool BombersGuide_HasMet(int32_t person);

// Bombers' Notebook guide: for every person, what to do on each day of the cycle, when and where. A person's
// tips stay hidden until Link has met them, as in the notebook. While the game's notebook is open, the selected
// person's steps are drawn on the notebook itself as sticky notes grouped by day (yellow to do, green once
// recorded). The full list also opens as a window from the menu.
class BombersNotebookGuideWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

#endif // MM3D_BOMBERS_NOTEBOOK_GUIDE_H
