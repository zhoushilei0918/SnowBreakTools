#pragma once

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Layout/Children.h"

namespace FModelRestoreWindow
{
inline constexpr float Width = 850.f;

inline void CountTabs(const TSharedRef<SWidget>& Widget, const SDockTab* Target, int32& Count, bool& Found)
{
    if (Widget->GetType() == TEXT("SDockTab"))
    {
        ++Count;
        Found |= &Widget.Get() == Target;
        return;
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
        CountTabs(Children->GetChildAt(Index), Target, Count, Found);
}

inline bool ResizeStandaloneTab(const TSharedPtr<SDockTab>& Tab)
{
    const TSharedPtr<SWindow> Window = Tab ? Tab->GetParentWindow() : nullptr;
    if (!Window) return false;

    // A default size is ignored when UE restores a saved docking layout.
    // Resize the actual window, but never an editor or a window shared with other tabs.
    int32 TabCount = 0;
    bool Found = false;
    CountTabs(Window->GetContent(), Tab.Get(), TabCount, Found);
    if (!Found || TabCount != 1) return false;

    if (Window->IsWindowMaximized()) Window->Restore();
    const FVector2D ClientSize = Window->GetClientSizeInScreen();
    Window->Resize(FVector2D(Width * Window->GetDPIScaleFactor(), ClientSize.Y));
    return true;
}
}
