#pragma once

// One place to open Far dialogs. DialogInit coordinates are intptr_t; passing
// `int` on x64 leaves garbage in the high half and Far rejects the dialog.

#include "plugin.hpp"

inline HANDLE FarOpenDialog(
    const PluginStartupInfo& info,
    const GUID& pluginId,
    const GUID& dialogId,
    FarDialogItem* items,
    size_t count,
    intptr_t width,
    intptr_t height,
    intptr_t (WINAPI *dlgProc)(HANDLE, intptr_t, intptr_t, void*) = nullptr,
    intptr_t param = 0)
{
    if (!info.DialogInit) return nullptr;
    SMALL_RECT rc{};
    if (info.AdvControl)
        info.AdvControl(pluginId, 24, 0, &rc);
    const intptr_t farW = (rc.Right > rc.Left) ? (rc.Right - rc.Left + 1) : 80;
    const intptr_t farH = (rc.Bottom > rc.Top) ? (rc.Bottom - rc.Top + 1) : 25;
    if (width >= farW) width = farW > 1 ? farW - 1 : 1;
    if (height >= farH) height = farH > 1 ? farH - 1 : 1;
    if (count && items && items[0].Type == DI_DOUBLEBOX) {
        items[0].X2 = width - 1;
        items[0].Y2 = height - 1;
    }
    return info.DialogInit(pluginId, dialogId, -1, -1, width, height, nullptr, items, count, 0,
                           FDLG_SMALLDIALOG, dlgProc, param);
}
