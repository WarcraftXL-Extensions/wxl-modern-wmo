// CMapObjGroup parse detour: publish OnWmoGroupLoad before the native sub-chunk walk.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "ExtensionApi.hpp"

#include "engine/events/Event.hpp"

#include "offsets/game/ADT.hpp"
#include "offsets/game/WMO.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    namespace ev  = wxl::events;
    namespace wmo = wxl::offsets::game::wmo;
    namespace adt = wxl::offsets::game::adt;

    wmo::WmoGroup_ParseFn g_origWmoGroup = nullptr;

    inline uint32_t Rd32(const void* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

    /**
     * @brief True when `id` names a resident LiquidType row with a MaterialID
     *        Liquid::CMaterialBank::GetMaterial (0x008A1FA0) can actually resolve -- see
     *        kLiquidTypeMaterialId in ADT.hpp. Same check wxl-adt's FixupMh2o runs on a terrain MH2O
     *        layer's type; the WMO group's own liquid id needs it for the identical reason, just from
     *        a completely different source file, so it is duplicated here rather than shared -- the two
     *        extensions do not otherwise depend on each other.
     */
    bool LiquidTypeUsable(uint32_t id)
    {
        const uint32_t minId = Rd32(reinterpret_cast<void*>(adt::kLiquidTypeDbMinId));
        const uint32_t maxId = Rd32(reinterpret_cast<void*>(adt::kLiquidTypeDbMaxId));
        if (id < minId || id > maxId) return false;
        const uint8_t* rows = reinterpret_cast<uint8_t*>(Rd32(reinterpret_cast<void*>(adt::kLiquidTypeDbRows)));
        if (!rows) return false;
        const uint32_t row = Rd32(rows + (id - minId) * 4u);
        if (!row) return false;
        const uint32_t materialId = Rd32(reinterpret_cast<void*>(row + adt::kLiquidTypeMaterialId));
        return materialId >= 1 && materialId <= 3;
    }

    /**
     * @brief Detours WMO group parse, emitting OnWmoGroupLoad before the native sub-chunk walk.
     *
     * The join point of the sync and async group-load paths, before the sub-chunk walk, so a subscriber
     * may reshape the group buffer in place; the native walk then reads the reshaped bytes.
     *
     * After the native walk resolves the group's liquid id (MOGP+0x48, verbatim for a modern-format
     * root or through the legacy 1..20 family table otherwise -- either way landing in kOffGroupLiquidType),
     * check it the same way FixupMh2o checks a terrain layer's type: row exists AND its MaterialID is
     * one GetMaterial's switch handles. A served liquid catalogue wider than classic's can resolve to a
     * row outside that range; left alone it null-derefs at draw time (CLiquidBank::DrawPass, 0x008A2240).
     * @param group  map-object group whose buffer was just read.
     * @param edx    unused register slot for the thiscall convention.
     */
    void __fastcall hkWmoGroupParse(void* group, void* edx)
    {
        ev::WmoGroupLoadArgs a{ group };
        wxl_wmo::g_api->Emit(uint32_t(ev::Event::OnWmoGroupLoad), &a);
        g_origWmoGroup(group, edx);

        auto* liquidType = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(group) + wmo::kOffGroupLiquidType);
        if (*liquidType != 0 && !LiquidTypeUsable(*liquidType))
            *liquidType = 1; // same "defaulting to water" convention the native id-not-found path uses
    }
}

namespace wxl_wmo
{
    bool InstallWmoGroupParse()
    {
        HookAttach("WmoGroupParse", wmo::kGroupParse, &hkWmoGroupParse, &g_origWmoGroup);
        return true;
    }
}
