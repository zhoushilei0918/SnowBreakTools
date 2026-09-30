#include "Modules/ModuleManager.h"
#include "SFModelRestorePanel.h"
#include "FModelRestoreWindow.h"
#include "IAnimationBlueprintEditor.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "BlueprintEditorContext.h"
#include "ToolMenus.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FModelAnimRestore"
class FFModelAnimRestoreModule : public IModuleInterface
{
    TSharedPtr<FSlateStyleSet> Style;
    TArray<TWeakPtr<FTabManager>> Managers;
    static FName TabId(bool Physics) { return Physics?FName("FModelRestore.Physics"):FName("FModelRestore.Poses"); }
    static FText Title(bool Physics) {return Physics?LOCTEXT("PhysicsTitle","Import Kawaii Physics"):LOCTEXT("PoseTitle","Import Pose Drivers / Pose Assets");}
public:
    virtual void StartupModule() override
    {
        if (IsRunningCommandlet()) return;
        for (bool Physics : {false,true}) FTabManager::RegisterDefaultTabWindowSize(TabId(Physics),FVector2D(FModelRestoreWindow::Width,600));
        Style=MakeShared<FSlateStyleSet>(TEXT("FModelAnimRestoreStyle"));
        Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("FModelAnimRestore"))->GetBaseDir()/TEXT("Resources"));
        Style->Set("FModelAnimRestore.Menu",new FSlateImageBrush(Style->RootToContentDir(TEXT("snow_break_logo512"),TEXT(".png")),FVector2D(20,20)));
        FSlateStyleRegistry::RegisterSlateStyle(*Style);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FFModelAnimRestoreModule::Menus));
    }
    virtual void ShutdownModule() override
    {
        if (IsRunningCommandlet()) return;
        for (bool Physics : {false,true}) FTabManager::UnregisterDefaultTabWindowSize(TabId(Physics));
        UToolMenus::UnRegisterStartupCallback(this); UToolMenus::UnregisterOwner(this);
        for (auto& Weak:Managers) if(auto Manager=Weak.Pin())
            for(bool Physics:{false,true})Manager->UnregisterTabSpawner(TabId(Physics));
        if(Style)FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
        Style.Reset();
    }
    void Open(TWeakPtr<IAnimationBlueprintEditor> WeakEditor,bool Physics)
    {
        auto Editor=WeakEditor.Pin(); if(!Editor)return;
        auto Manager=Editor->GetTabManager(); if(!Manager)return;
        if(!Manager->HasTabSpawner(TabId(Physics)))
        {
            Manager->RegisterTabSpawner(TabId(Physics),FOnSpawnTab::CreateLambda([WeakEditor,Physics](const FSpawnTabArgs&)
            {
                return SNew(SDockTab).TabRole(ETabRole::PanelTab)[SNew(SFModelRestorePanel).Editor(WeakEditor).Physics(Physics)];
            })).SetDisplayName(Title(Physics)).SetIcon(FSlateIcon("FModelAnimRestoreStyle","FModelAnimRestore.Menu")).SetMenuType(ETabSpawnerMenuType::Hidden);
            Managers.AddUnique(Manager);
        }
        FModelRestoreWindow::ResizeStandaloneTab(Manager->TryInvokeTab(TabId(Physics)));
    }
    void Populate(FToolMenuSection& Section,bool Toolbar)
    {
        const auto* Context=Section.FindContext<UAssetEditorToolkitMenuContext>();
        auto Toolkit=Context?Context->Toolkit.Pin():nullptr;
        if (!Toolkit)
            if (const auto* BlueprintContext=Section.FindContext<UBlueprintEditorToolMenuContext>()) Toolkit=BlueprintContext->BlueprintEditor.Pin();
        if(!Toolkit || Toolkit->GetToolkitFName()!=IAnimationBlueprintEditor::GetAnimationBlueprintEditorToolkitName())return;
        TWeakPtr<IAnimationBlueprintEditor> Editor=StaticCastSharedPtr<IAnimationBlueprintEditor>(Toolkit);
        for (bool Physics:{false,true})
        {
            const FName Name=Physics?TEXT("FModelPhysicsImport"):TEXT("FModelPoseImport");
            const FUIAction Action(FExecuteAction::CreateRaw(this,&FFModelAnimRestoreModule::Open,Editor,Physics),FCanExecuteAction::CreateLambda([Editor]{return Editor.IsValid();}));
            const FSlateIcon Icon("FModelAnimRestoreStyle","FModelAnimRestore.Menu");
            if(Toolbar)Section.AddEntry(FToolMenuEntry::InitToolBarButton(Name,Action,Physics?LOCTEXT("PhysicsButton","Kawaii Import"):LOCTEXT("PoseButton","Pose Import"),Title(Physics),Icon));
            else Section.AddMenuEntry(Name,Title(Physics),Title(Physics),Icon,Action);
        }
    }
    void Menus()
    {
        FToolMenuOwnerScoped Owner(this);
        // The asset-editor context supplies the actual owner. No global or level-editor entry is registered.
        UToolMenus::Get()->ExtendMenu("AssetEditor.AnimationBlueprintEditor.MainMenu.Tools")->FindOrAddSection("FModelAnimRestore")
            .AddDynamicEntry("FModelRestorePanels",FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& Section){Populate(Section,false);}));
        UToolMenus::Get()->ExtendMenu("AssetEditor.AnimationBlueprintEditor.ToolBar")->FindOrAddSection("FModelAnimRestore")
            .AddDynamicEntry("FModelRestorePanels",FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& Section){Populate(Section,true);}));
    }
};
IMPLEMENT_MODULE(FFModelAnimRestoreModule,FModelAnimRestore)
#undef LOCTEXT_NAMESPACE
