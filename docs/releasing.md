# 版本与二进制发布

`.github/workflows/release.yml` 构建一个 Python sdist，并从同一份 sdist 为五个平台 / 架构组合、四个 CPython 版本构建 20 个 wheel：Linux x64/ARM64、Windows x64、macOS Intel/Apple Silicon，CPython 3.11–3.14。发布前检查制品矩阵完整性、版本、提交、源码与 API 摘要，并生成发布清单和校验和。

C++ 动静态库构建和测试由普通提交的 `cpp-ci.yml` 执行，Python 契约与互操作检查由 `python-ci.yml` 执行。Release 不再重复调用 Python CI，也不再构建或上传独立 C++ 库包。

## 发布版本

推荐修改根目录 `VERSION` 为新的 `X.Y.Z`（例如 `0.1.1`），将变更合入 `main`。CI 自动生成 `v0.1.1` 标签、GitHub Release、更新说明、20 个 wheel、1 个 sdist、`release-manifest.json` 和 `SHA256SUMS`。标签绑定到触发构建的提交，不会使用构建完成时的新 `main` 提交。

也可主动推送标签来发布当前版本，包括首次发布：

```sh
# VERSION 必须已经是 0.1.0，且该提交已包含 release.yml。
git tag v0.1.0
git push origin v0.1.0
```

标签必须严格等于 `v` 加 `VERSION`。仅支持正式的三段数字版本；不支持 `-rc` 等预发布后缀。不要移动已有标签来重新发布；修改代码后应提升 `VERSION`。

工作流使用仓库自带的 `GITHUB_TOKEN`，只有发布任务具有 `contents: write` 权限，无需额外配置 PAT。仓库规则若限制创建 `v*` 标签，需允许该发布任务创建标签。由这个 token 自动创建的标签不会触发第二轮 Actions 工作流。

主动推送标签前，先将标签对应的提交推送至远端 `main`。目标提交相对远端默认分支修改 `.github/workflows/` 时，GitHub 创建 Release 可能要求 `GITHUB_TOKEN` 无法获得的 Workflows 写权限并返回 403；参见 [GitHub Release API 权限说明](https://docs.github.com/en/rest/releases/releases#create-a-release)。

上传先进入 Release 草稿，全部上传成功后才公开。上传失败可重跑失败任务，继续完成草稿。同一版本的发布串行执行；已有正式 Release 会保留原有产物，重复运行跳过发布。已有标签指向其他提交时会报错，须提升版本。

## 自动发布到 PyPI

GitHub Release 成功后，`publish-pypi` 任务从已公开的 Release 下载同一批 20 个 wheel、1 个 sdist、发布清单和 `SHA256SUMS`，先检查原始校验和，再核对版本、提交与完整矩阵。仅 wheel 和 sdist 会进入 PyPI 上传目录；任务不会重新构建发行包。PR 和手动验证运行不发布到 PyPI，GitHub Release 失败时也不会执行 PyPI 发布。

首次启用前，需要完成以下账户配置：

1. 在 [仓库 Environments 设置](https://github.com/600888/dlt698/settings/environments) 创建名称为 `pypi` 的环境。允许部署的分支和标签需覆盖 `main` 与 `v*`；如需全自动发布，不设置 required reviewers。
2. 如果 PyPI 已有你管理的 `dlt698` 项目，在 [项目 Publishing 页面](https://pypi.org/manage/project/dlt698/settings/publishing/) 添加 GitHub Trusted Publisher。如果项目尚未创建，在 [账户 Publishing 页面](https://pypi.org/manage/account/publishing/) 添加 pending publisher，项目名填写 `dlt698`；名称必须可用，首次成功上传会创建项目。
3. 两种情况都使用下面的配置，环境名称必须与工作流完全一致：

| PyPI 字段 | 值 |
| --- | --- |
| PyPI Project Name（仅 pending publisher） | `dlt698` |
| Owner | `600888` |
| Repository name | `dlt698` |
| Workflow name | `release.yml`（仅文件名） |
| Environment name | `pypi` |

任务通过 `id-token: write` 获取 OIDC 身份，并使用 `pypa/gh-action-pypi-publish` 交换短期上传凭据，无需设置 `PYPI_API_TOKEN` 或账户密码。权限仅授予 PyPI 发布任务。配置方式见 [已有项目的 Trusted Publisher](https://docs.pypi.org/trusted-publishers/adding-a-publisher/) 和 [首次创建项目的 pending publisher](https://docs.pypi.org/trusted-publishers/creating-a-project-through-oidc/)。

PyPI 上传不是整个版本的原子操作，网络中断可能只上传部分文件；修复配置或网络问题后，在原运行中重跑失败任务即可继续。`skip-existing: true` 保留 PyPI 已有的同名文件，补传其余发行包；不会覆盖已有文件。同版本的 PyPI 发布串行执行，重跑始终使用 GitHub Release 的原始附件。若 PyPI 的同名文件并非本次 Release 的产物，不能用重跑替换，须提升 `VERSION`。

合入新增 PyPI 任务的提交后，需要发布包含此工作流的新版本；重跑旧 `v1.0.0` 运行仍会使用旧工作流，不会新增 PyPI 任务。完成真实发布后，用户可使用 `python -m pip install dlt698` 安装。账户授权与实际 PyPI 上传需在 GitHub Actions 中验收，本地检查不能代替。

## 发布前验证

在 Actions 中手动运行 **Release** 会构建 sdist，并构建和测试当前选中 ref 的 wheel，仅上传 Actions artifacts，不创建标签或 Release。涉及版本、工作流或 Python/C++ 源码的 PR 也会运行相同构建。Actions artifacts 使用仓库默认保留期限，正式 Release 的附件不受此期限影响。

下载后先用 `sha256sum --check --ignore-missing SHA256SUMS` 校验，再用 `python -m pip install <wheel路径>` 安装匹配平台和解释器的 wheel。macOS 可用 `shasum -a 256 -c SHA256SUMS`，Windows 可用 PowerShell `Get-FileHash <包路径> -Algorithm SHA256` 比对对应条目。

## 本地 C++ 包布局与使用

使用下面的本地打包命令生成的 C++ 包具有一个 `dlt698-<版本>-<平台>-<库类型>/` 根目录：

```text
include/dlt698/       公开头文件和生成的导出头
lib/                 五个组件的静态库、共享库或 Windows 导入库
lib/cmake/dlt698/     find_package 配置和导出目标
bin/                 Windows 动态包中的 DLL
share/dlt698/        项目和 Asio 许可证
VERSION
README.md
```

静态和动态包均包含 core、session、service、transport、app，使用不同的安装前缀。头文件随各个包提供，不需要另行下载；内部 Asio 头文件不是公开接口，不需要安装给消费方。Windows 静态库也使用动态 MSVC 运行库 `/MD`；这里的静态 / 动态指 dlt698 的链接方式。

使用打包脚本生成的独立 `.sha256` 文件校验压缩包，再解压并配置消费方：

```sh
sha256sum --check dlt698-1.0.0-linux-x64-gcc-static.tar.gz.sha256
cmake -S your-app -B your-app/build -DCMAKE_PREFIX_PATH=/absolute/path/to/dlt698-1.0.0-linux-x64-gcc-static
```

消费方通过 `find_package(dlt698 CONFIG REQUIRED)` 和 `dlt698::dlt698`（或细分组件）链接。工具链、运行库和 ABI 要求见 [README](../README.md#预编译发布包)。

## 本地打包

以下示例使用新的构建目录，确保安装缓存不混入产物：

```sh
cmake -S . -B build/release-local -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=ON -DDLT698_BUILD_TRANSPORT=ON -DDLT698_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build/release-local --config Release --parallel 2
ctest --test-dir build/release-local -C Release --output-on-failure
cmake -DBUILD_DIR=build/release-local -DOUTPUT_DIR=build/release-local/packages -DPLATFORM=linux-x64-gcc -DLINKAGE=static -P cmake/release-package.cmake
```

`PLATFORM` 按实际工具链选择 `linux-x64-gcc`、`linux-arm64-gcc`、`windows-x64-msvc`、`macos-x64-appleclang` 或 `macos-arm64-appleclang`。动态构建需同时设置 `BUILD_SHARED_LIBS=ON`、`LINKAGE=shared`；Windows 使用 Visual Studio 生成器及 `-A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`。

打包脚本调用 `cmake --install`，检查五个组件的库、导出头、CMake 配置和许可证是否齐全，再生成压缩包及独立 `.sha256` 文件。Unix 使用 tar.gz 保留版本化共享库符号链接，Windows 使用 zip。脚本拒绝复用已有的 `release-stage/<包名>` 目录，重新打包时请使用新的构建目录，或先手动清理该暂存目录。
