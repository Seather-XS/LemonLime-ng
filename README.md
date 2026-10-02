<a href="https://project-lemonlime.github.io/Project_LemonLime/"><img src="assets/icons/lemon-lime.png" align=right /></a>

# gengen-tuack

[Project LemonLime](https://github.com/Project-LemonLime/Project_LemonLime) 的一个分支，面向 gengen 风格的比赛：
在原有评测能力之上，补齐了**赛区**、**题面**与**一键打包**这几件事。

需要 Qt 6.8 或更高版本。主要在 Windows + MinGW 上开发与验证，其它平台沿用上游的构建方式。

## 与原版的区别

评测本身与上游一致，这个分支主要在比赛的组织方式上做了补齐：

-   **比赛日布局**：试题数据放在 `problem/<题>/{data,down,graders,gen}`，选手代码放在 `answers/<赛区>/<选手>/`；打开旧布局的比赛日时会自动迁移。
-   **赛区**：可以按赛区组织选手，成绩表、统计和导出都会带上赛区，也能按赛区拆开。
-   **题面**：内置题面编辑器，写 Markdown，用 pandoc + LaTeX 编译成 PDF，自带 ccpc / noi / noi new 模板，右侧即时预览；导出的 PDF 名字可以自己写模板。
-   **导出**：新增「导出」选项卡，把比赛日一键打成 zip —— 选手目录、测试数据、选手代码三类包；选手目录里可以自选 `statement/` 下的题面文件，还能额外套一层目录、压缩包内再套压缩包、密码加密。
-   **成绩与统计**：固定导出到 `<比赛日>/dist/reports/`；启用赛区时每个赛区各出一份。

## 安装

Windows 上可以直接用仓库里的 `build.cmd` 构建（需要 Qt 6、MinGW、CMake、Ninja）：

```powershell
.\build.cmd            # 构建到 build\，产物是 build\lemon.exe
.\build.cmd -NoLto     # 关掉 LTO，链接快很多
.\build.cmd -Package   # 顺便打包成带 Qt 运行库的 zip（放到 dist\）
```

`build.cmd` / `package.cmd` 只是把 `build.ps1` / `package.ps1` 换成
`powershell -ExecutionPolicy Bypass -File ...` 调用：Windows 客户端默认的执行策略是 `Restricted`，
直接 `.\build.ps1` 会报「因为在此系统上禁止运行脚本」。
不想用包装脚本的话，二选一：

```powershell
# A. 只对当前窗口生效
powershell -NoProfile -ExecutionPolicy Bypass -File .\package.ps1

# B. 只对本用户放开（不需要管理员）
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

其它平台的依赖与构建方式见 [BUILD.md](BUILD.md)。

### 打包成能直接运行的完整软件

`package.ps1` 会在构建之后把 Qt 运行库铺到 `lemon.exe` 旁边，并删掉用不到的文件，
让 `build\` 拷到别的机器上也能直接跑：

```powershell
.\package.cmd                 # 构建 + 就地部署到 build\（含题面模板），最后自检
.\package.cmd -NoBuild        # 不重新构建，只部署 / 精简
.\package.cmd -OutDir .\out   # 生成一个干干净净的发布目录（不含任何构建中间产物）
.\package.cmd -PruneBuildJunk # 就地部署时顺便清掉 *.obj / CMakeFiles\ 等（下次构建会完整重编）
```

（用 `package.ps1` 时要先解决执行策略的问题，两种办法见上一节。）

它会删掉这些用不到的东西：`opengl32sw.dll`、`D3Dcompiler_47.dll`、Qt 自带的几十种语言翻译
（只留 `zh_CN` / `zh_TW`，界面文字用的是程序内嵌的翻译）、多余的图片格式插件（只留 SVG）、
调试符号，以及 `tls` / `networkinformation` / `generic` 这些插件目录。

每次部署完都会跑一遍 `lemon.exe --self-test`（把 PATH 里的 Qt / MinGW 全部去掉），
确认平台插件、SVG 图标、题面模板、内嵌翻译和主窗口都正常；要发布前能放心。

## 快速使用

新建比赛 → 新建比赛日 → 添加试题 → 评测，与上游相同。这个分支多出来的部分集中在三个选项卡：

-   **题面**：编辑 Markdown，点「导出并打开 PDF」编译题面（编译完直接打开），PDF 放在 `<比赛日>/statement/` 下。文件名可以在「PDF 文件名」里自定义，支持占位符：`<day>` 比赛日文件名、`<title-day>` 比赛日标题、`<title>` 比赛标题（默认 `statement.pdf`）。
-   **导出**：选择包类型与选项，导出到 `<比赛日>/dist/export/`；选手目录包里可以在「题面文件」下拉框里挑 `statement/` 下的哪个文件进包。
-   **统计**：查看整场统计，导出到 `<比赛日>/dist/reports/`。

比赛的导出物统一放在 `<比赛日>/dist/` 下；早期版本遗留在根目录的 `export/`、`reports/` 会在打开比赛日时自动清掉。

题面 Markdown 的写法、各选项的含义等细节，见用户手册与界面内的提示。

## 用户手册

[在线用户手册](https://project-lemonlime.github.io/Project_LemonLime/)（上游版本；本分支新增的功能以上一节的说明与界面提示为准）。

## Credit

本项目按 GPL-3.0-or-later 授权，见 [LICENSE](LICENSE)。

```
Copyright (c) 2019-2022 Project LemonLime.

Libraries and other files that have been used in LemonLime are listed below:

Copyright (c) 2020 Itay Grudev (@itay-grudev): SingleApplication (MIT)

Copyright (c) 2020 Qv2ray Development Group (@Qv2ray): Design of Translator/Log, Project Structure and CI files (GPLv3)
```
