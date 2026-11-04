#include "QuestRenderer.h"

#include <map>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

namespace QuestUi {

namespace {

struct LoadedAsset {
    bool tried = false;
    bool ok = false;
};

std::map<std::string, LoadedAsset> sAssets;

std::string GuiName(const char* name) {
    return std::string("QuestMenu/") + name;
}

} // namespace

ImTextureID Asset(const char* name) {
    LoadedAsset& asset = sAssets[name];
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (!asset.tried) {
        asset.tried = true;
        std::string path = std::string("overlays/questmenu/") + name + ".png";
        auto archives = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
        if (archives != nullptr && archives->HasFile(path)) {
            gui->LoadTextureFromRawImage(GuiName(name), path);
            asset.ok = gui->HasTextureByName(GuiName(name));
        }
        if (!asset.ok) {
            SPDLOG_WARN("Quest menu: missing {}", path);
        }
    }
    return asset.ok ? gui->GetTextureByName(GuiName(name)) : nullptr;
}

ImVec2 AssetSize(const char* name) {
    if (Asset(name) == nullptr) {
        return ImVec2(0, 0);
    }
    return Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureSize(GuiName(name));
}

bool NineSlice(const Page& pg, const char* name, ImVec2 a, ImVec2 b, float texBorder, float border, ImU32 tint) {
    ImTextureID id = Asset(name);
    if (id == nullptr) {
        return false;
    }
    ImVec2 size = AssetSize(name);
    float u = texBorder / size.x, v = texBorder / size.y;
    float xs[4] = { a.x, a.x + border, b.x - border, b.x };
    float ys[4] = { a.y, a.y + border, b.y - border, b.y };
    float us[4] = { 0.0f, u, 1.0f - u, 1.0f };
    float vs[4] = { 0.0f, v, 1.0f - v, 1.0f };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            if (xs[col + 1] <= xs[col] || ys[row + 1] <= ys[row]) {
                continue;
            }
            pg.dl->AddImage(id, pg.P(xs[col], ys[row]), pg.P(xs[col + 1], ys[row + 1]), ImVec2(us[col], vs[row]),
                            ImVec2(us[col + 1], vs[row + 1]), tint);
        }
    }
    return true;
}

bool Png(const Page& pg, const char* name, ImVec2 a, ImVec2 b, ImU32 tint, float tile) {
    ImTextureID id = Asset(name);
    if (id == nullptr) {
        return false;
    }
    if (tile <= 0.0f) {
        pg.dl->AddImage(id, pg.P(a.x, a.y), pg.P(b.x, b.y), ImVec2(0, 0), ImVec2(1, 1), tint);
        return true;
    }
    // Tiled: whole tiles, with the last row and column cut to fit.
    for (float y = a.y; y < b.y; y += tile) {
        for (float x = a.x; x < b.x; x += tile) {
            float x1 = std::min(x + tile, b.x), y1 = std::min(y + tile, b.y);
            pg.dl->AddImage(id, pg.P(x, y), pg.P(x1, y1), ImVec2(0, 0), ImVec2((x1 - x) / tile, (y1 - y) / tile),
                            tint);
        }
    }
    return true;
}

} // namespace QuestUi
