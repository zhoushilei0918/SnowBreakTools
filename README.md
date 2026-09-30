# FModel Animation Restore 1.3.1 — UE 5.8

为已经导入 FBX、已经创建动画蓝图的角色还原 Pose Driver 补偿。补偿面板只需选择 **abpp.json** 和 **POSE 文件夹**，直接使用 UnrealPSKPSA 导入动画，再生成 UE 原生 Pose Asset 和 Pose Driver。

本版只调整补偿导入，Kawaii Physics 面板保留原来的使用方式。插件依赖项目中现有的 **UnrealPSKPSA** 和 **KawaiiPhysics**，本工程使用 KawaiiPhysics 1.21.0。不需要 Control Rig。

## 使用补偿面板

1. 先导入角色 FBX，用它的 Skeleton 创建并打开 Animation Blueprint，设置好预览模型。
2. 在该动画蓝图编辑器工具栏点击 **补偿导入 / Pose Import**。也可从该编辑器的 Tools 菜单打开。
3. 选择角色的 `girlxxx_abpp.json`。
4. 选择包含对应 `*_POSE.psa` 和 `*_POSE_PoseAsset.json` 的文件夹。两项选择下方会显示可复制的完整路径；右侧文件夹按钮可在资源管理器中定位 JSON 或打开 POSE 目录。
5. 点击 **读取分组 / Read groups**，勾选需要的补偿组。可以同时选择左右手臂、左右腿，也可分批导入。每组会显示实际匹配的两个文件名及姿态数量。
6. 点击分组列表上方的 **导入动画并创建 Pose Asset**。插件自动导入每组 PSA，并按照 PoseAsset JSON 的姿态名称和顺序创建 Pose Asset。两个只读资产插槽随后显示生成的动画和 Pose Asset，可打开或在内容浏览器中定位。
7. 点击 **添加节点到当前蓝图**。插件按 abpp 的顺序与配置添加 Pose Driver，并连接所选组内部的姿态链。
8. 将新节点链的输入、输出接入自己的动画流程，编译并保存蓝图。
9. 需要挂到模型后处理时，点击添加节点按钮右侧的 **设为网格体后处理动画蓝图**。插件先编译当前蓝图，再修改当前预览网格体的 Post Process Anim Blueprint；保存网格体即可保留。已有后处理会被替换，Ctrl+Z 可恢复原设置。编译失败或 Skeleton 不匹配时不会修改设置。

**无需提前手动导入 POSE PSA，也无需手动创建 Pose Asset。** 普通行走、攻击等参考动作继续使用蓝图内原来的动画流程，不是补偿导入的输入。

当前蓝图和预览模型通过只读资产插槽展示，绑定打开面板的编辑器。Target Graph 可选择当前蓝图内的动画图表；它是蓝图内部对象，不是独立资产。

所有补偿输出自动保存在**当前动画蓝图所在目录的 POSE 子文件夹**。再次导入会生成带数字后缀的新资产，不覆盖旧资产。面板仅显示两个来源的路径用于查看与定位，不要求填写 FModel 根目录、Skeleton JSON 或输出路径。

资产插槽在导入前为空；读取分组仅检查源文件，不会生成资产。面板中的生成资产插槽用于查看结果，不用于手动替换输入。

## FModel 文件准备

以 Girl022 为例，补偿需要：

```text
任意文件夹/
  girl022_abpp.json
  POSE/
    girl022_Forearm_L_POSE.psa
    girl022_Forearm_L_POSE_PoseAsset.json
    girl022_Forearm_R_POSE.psa
    girl022_Forearm_R_POSE_PoseAsset.json
    girl022_Calf_L_POSE.psa
    girl022_Calf_L_POSE_PoseAsset.json
    girl022_Calf_R_POSE.psa
    girl022_Calf_R_POSE_PoseAsset.json
```

在 FModel 中导出 abpp 和 PoseAsset 的属性 JSON，以及 POSE 动画的 PSA。保留源文件名，放在同一个 POSE 文件夹中即可；abpp 可放在其他位置。补偿导入**不要求 Game/Content 目录结构、不读取 Skeleton.json，也不需要 AnimSequence.json**。

插件按 abpp 中引用的 Pose Asset 名称匹配 JSON；按 JSON 的 SourceAnimation 名称匹配 PSA，如果源 JSON 没有该字段，则将 Pose Asset 名称末尾的 `_PoseAsset` 去掉来匹配同名 PSA。

每个 PSA 需要包含一段动画，采样数量必须与对应 PoseAsset JSON 的姿态数量相同。UnrealPSKPSA 负责文件读取、坐标转换以及与当前 FBX 模型的骨骼匹配。本插件负责姿态命名、Pose Asset 创建和 abpp 节点配置。

源模型必须与当前 FBX 的骨架相匹配。普通版、和谐版、高模版或技能变身版能否混用，取决于实际骨骼名称和父级，不能仅按角色名判断。插件不会自动重定向不同骨架，也不会修复 FBX 导入时改动的骨骼局部坐标系。

当前支持非 Additive Pose Asset、DrivePoses 和可从输出追踪至输入的线性 Pose Driver 链。其他类型会明确报错。

## Kawaii Physics 面板

在动画蓝图中点击 **物理导入 / Kawaii Import**，选择 `ABP_Girlxxx_Phy.json`，读取分组、准备所选物理组，再添加到当前蓝图。

该流程仍使用 FModel 导出的 Game/Content 目录结构及 JSON 中引用的 Skeleton JSON；通常自动定位，也可选择根目录。物理导入不创建 Pose Asset，并同时添加 Local To Component / Component To Local 空间转换节点。

可参考以下接线顺序，再按自己原有图表调整：

`基础姿态 → Local To Component → Kawaii Physics → Component To Local → Pose Driver → Output Pose`

KawaiiPhysics 能对应的根骨骼、排除骨骼、阻尼、刚度、重力、风、碰撞体、Alpha、LOD 等内联配置会还原。原游戏定制的 Wave、Recoil、ExternalAlpha、ChangeFactor 等行为没有标准插件中的直接等价实现；外部资产、外部曲线和动态蓝图绑定不会自动重建。旧资源采用当前插件提供的旧版重力模式。未映射字段记录在报告中。

分组操作区按“读取分组 / 全选 / 清空选择 → 分隔横线 → 导入动画并创建 Pose Asset → 分组列表”排列。添加节点与设置后处理按钮位于列表下方。后处理按钮仅设置网格体属性，不改变蓝图连线；接收上游姿态的后处理图应由 Input Pose 接入补偿链。

## 编辑与本地化

两个面板只在 Animation Blueprint 编辑器提供入口。每个面板绑定自己的蓝图和预览网格体。更换预览模型后需要重新读取分组。

创建资产时不改图表，添加节点时保留原来的 Output Pose 连线。添加操作支持 Ctrl+Z / Ctrl+Y；已保存的动画和 Pose Asset 不会随节点撤销被删除。

支持 UE 原生英语、简体中文本地化，跟随 Editor Preferences 的 Editor Language。插件图标和菜单图标沿用提供的图片。发布时需要保留 Resources 和 Content/Localization。

完整诊断报告保存在项目 `Saved/FModelAnimRestore` 中，用于排查源文件、输出资产和未映射配置。补偿面板只显示简短结果。

## 测试与移植

自动化测试入口：`FModelAnimRestore.*`。包含面板构建、中英文、Girl022 多组补偿、独立物理导入、撤销重做、骨骼父级校验、独立 PSA 文件夹和 FBX 模型验证。

本项目 1.3.0 验证结果：6 项通过、0 项失败、0 项警告，报告为 `Saved/FModelAnimRestore/AutomationV130Final/index.json`。1.3.1 共 7 项全部通过，0 项警告，新增后处理赋值、替换、撤销重做与 Skeleton 不匹配保护测试；最新报告为 `Saved/FModelAnimRestore/AutomationV131/index.json`。额外完成 160 项补偿采样、988 项物理配置及 76 次角度求值检查，结果为 `Saved/FModelAnimRestore/Girl022Validation.json`。

PSA 的实际导入值另有逐帧检查；与未压缩 PoseAsset JSON 比较时，位置容差为 0.05 cm，并将 JSON 四元数归一化后比较方向，以容纳导出动画的采样精度差异。

测试数据使用项目内独立的 FModelRestoreTests 目录。`Tests/prepare_girl022.py` 准备基础测试模型，`Tests/prepare_girl022_fbx.py` 将测试模型导出为 FBX 后重新导入；FBX 导出需要启用渲染支持。测试脚本中的源文件位置需按实际环境调整。

复制整个 FModelAnimRestore 文件夹到其他 UE 5.8 项目的 Plugins，并安装启用 UnrealPSKPSA 和 KawaiiPhysics。附带 Win64 编辑器二进制；引擎构建号或依赖插件 API 不同时需要重新编译。

旧的 Restore / Commandlet 接口仍用于自动化生成完整蓝图；补偿导入需额外指定 `PoseFolder=`。可视化面板不要求使用此接口。生成结果使用 UE 原生动画资产与节点，编辑器导入插件不参与游戏运行。
