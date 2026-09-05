# Building CaptureView

[日本語](#japanese)

CaptureView is a native x64 Windows application. WSL is convenient for editing
the source, but the application must be built with the Windows MSVC toolchain.

## Install a minimal build environment

Install these tools on Windows:

1. **Visual Studio 2022 Build Tools**
2. **CMake 3.24 or later**
3. **Ninja** (recommended, but optional)

The installers can be obtained with WinGet:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
```

In Visual Studio Installer, open **Modify**, then select only the components
needed for this project:

- MSVC v143 - VS 2022 C++ x64/x86 build tools (latest)
- A supported Windows 10 or Windows 11 SDK

The full Visual Studio IDE, .NET desktop workloads, ATL, MFC, and the Windows
Driver Kit are not required.

Open **x64 Native Tools Command Prompt for VS 2022** or **Developer PowerShell
for VS 2022**, then verify the tools:

```powershell
cl
cmake --version
ninja --version
```

`cl` should identify an x64 compiler. If a newly installed command is not
found, close and reopen the terminal.

## Build with Ninja

From the repository directory:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run:

```powershell
.\build\CaptureView.exe
```

Close CaptureView before rebuilding it. Otherwise the linker may report
`LNK1168` because the running executable cannot be overwritten.

## Build with the Visual Studio generator

Ninja is not required with this method:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\CaptureView.exe
```

## Create a portable package

After a Release build, create the same ZIP and SHA-256 file used by GitHub
Actions:

```powershell
.\tools\package_release.ps1
```

The script reads the version from `CMakeLists.txt` and writes the package under
`dist\`. The ZIP contains `CaptureView.exe`, `LICENSE.txt`, and `README.md`.

## Create a Microsoft Store package

The Store packaging workflow uses a public manifest template and injects the
Partner Center identity at build time. After an x64 Release build, run:

```powershell
.\tools\package_msix.ps1 `
  -IdentityName "<Package Identity Name>" `
  -Publisher "<Package Publisher>" `
  -PublisherDisplayName "<Publisher display name>" `
  -Version "1.1.0.0"
```

The script requires a Windows 10 or Windows 11 SDK and writes an unsigned
`.msix` plus a Store submission `.msixupload` under `dist-msix\`. The upload
file includes the Release PDB as an `.appxsym` when symbols are available.
Release builds retain compiler and linker optimization while producing a PDB
for Store crash analysis. The Store workflow treats missing symbols as an
error.

The `MSIX Store package` GitHub Actions workflow accepts the four-part version
as a manual input and reads these repository secrets:

- `MSIX_IDENTITY_NAME`
- `MSIX_PUBLISHER`
- `MSIX_PUBLISHER_DISPLAY_NAME`

The identifiers are package metadata rather than authentication credentials,
but they are intentionally not hard-coded in this public source tree. Never
store signing keys, certificate passwords, or Partner Center credentials in
source. Microsoft signs packages that pass Store certification; locally
installing an MSIX for testing requires a suitable test signature and trusted
certificate.

See [Testing the CaptureView MSIX package](msix-testing.md) for the local
test-signing findings, permission prompts, application-data behavior, and
certificate cleanup requirements.

## If Windows blocks a locally built executable

First confirm that the executable was built from source you reviewed. Do not
work around a security warning for an unknown binary.

### 1. Check for Mark of the Web

If the repository or executable came from a downloaded ZIP, Windows may have
attached Zone.Identifier metadata. Remove it only from the reviewed local
build:

```powershell
Get-Item .\build\CaptureView.exe | Unblock-File
```

For a Visual Studio Release build, adjust the path to
`.\build\Release\CaptureView.exe`.

`Unblock-File` only removes downloaded-file metadata. It does not bypass Smart
App Control, Windows Defender Application Control (WDAC), or an organization
policy.

### 2. Inspect the Code Integrity log

If Windows reports that Device Guard or an organizational policy blocked the
file, inspect the recent Code Integrity events:

```powershell
Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" -MaxEvents 20 |
  Select-Object TimeCreated, Id, Message |
  Format-List
```

Events such as 3077 or 3033 identify the blocked file and policy. On a managed
work or school PC, contact the administrator instead of changing the policy.

### 3. Smart App Control on a development PC

Smart App Control may block a newly generated unsigned executable, and Windows
does not provide an exception for one individual app. On a personally managed
development PC, developers may choose to disable Smart App Control under:

```text
Windows Security
  > App & browser control
  > Smart App Control settings
```

Disabling it reduces protection against untrusted applications. Keep Microsoft
Defender enabled, and do not recommend this as a normal installation step for
end users. Recent Windows versions may allow Smart App Control to be enabled
again from Windows Security.

For current behavior, see Microsoft's
[Smart App Control FAQ](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/smart-app-control-frequently-asked-questions).

Official Store builds are planned as MSIX packages signed by Microsoft. Direct
development and portable builds remain unsigned unless the publisher applies a
trusted code-signing certificate.

---

<a id="japanese"></a>

# CaptureViewのビルド

CaptureViewはx64 Windowsネイティブアプリケーションです。ソース編集にはWSLも
利用できますが、ビルドにはWindows側のMSVCツールチェーンを使用します。

## 最小構成のビルド環境

Windowsへ次のツールを導入します。

1. **Visual Studio 2022 Build Tools**
2. **CMake 3.24以降**
3. **Ninja**（推奨、必須ではありません）

WinGetからインストーラーを取得できます。

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
```

Visual Studio Installerの**変更**を開き、このプロジェクトに必要なコンポーネント
だけを選択します。

- MSVC v143 - VS 2022 C++ x64/x86ビルドツール（最新）
- サポート対象のWindows 10またはWindows 11 SDK

Visual Studio IDE本体、.NETデスクトップワークロード、ATL、MFC、Windows Driver
Kitは必要ありません。

**x64 Native Tools Command Prompt for VS 2022**または**Developer PowerShell for
VS 2022**を開き、ツールを確認します。

```powershell
cl
cmake --version
ninja --version
```

`cl`がx64コンパイラーと表示されることを確認してください。インストール直後に
コマンドが見つからない場合は、ターミナルを開き直します。

## Ninjaでビルド

リポジトリのディレクトリで実行します。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\CaptureView.exe
```

再ビルド前にCaptureViewを終了してください。実行中のままだと、リンカーがEXEを
上書きできず`LNK1168`になることがあります。

## Visual Studioジェネレーターでビルド

この方法ではNinjaは不要です。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\CaptureView.exe
```

## ポータブルパッケージの作成

Releaseビルド後、GitHub Actionsと同じZIPおよびSHA-256ファイルを作成できます。

```powershell
.\tools\package_release.ps1
```

スクリプトは`CMakeLists.txt`からバージョンを取得し、`dist\`以下へ出力します。
ZIPには`CaptureView.exe`、`LICENSE.txt`、`README.md`が含まれます。

## Microsoft Storeパッケージの作成

Storeパッケージでは、公開可能なManifestテンプレートへビルド時にPartner Center
の製品IDを注入します。x64 Releaseビルド後、次のように実行します。

```powershell
.\tools\package_msix.ps1 `
  -IdentityName "<Package Identity Name>" `
  -Publisher "<Package Publisher>" `
  -PublisherDisplayName "<Publisher display name>" `
  -Version "1.1.0.0"
```

Windows 10またはWindows 11 SDKが必要です。`dist-msix\`以下へ無署名の
`.msix`とStore提出用`.msixupload`を生成します。Release PDBが存在する場合、
提出ファイルには`.appxsym`としてシンボルも含まれます。Releaseビルドは最適化を
維持したままStoreのクラッシュ解析用PDBを生成し、Store workflowではシンボルが
存在しない場合をエラーとして扱います。

GitHub Actionsの`MSIX Store package`ワークフローは、4部形式のバージョンを
手動入力として受け取り、次のRepository secretsを使用します。

- `MSIX_IDENTITY_NAME`
- `MSIX_PUBLISHER`
- `MSIX_PUBLISHER_DISPLAY_NAME`

これらの識別値は認証情報ではありませんが、公開ソースには直接記録しない方針
です。署名鍵、証明書パスワード、Partner Centerの認証情報はソースへ保存しないで
ください。Store審査を通過したパッケージはMicrosoftが署名します。ローカルで
MSIXをインストールして試験する場合は、別途テスト署名と信頼済み証明書が必要です。

ローカルのテスト署名、アクセス許可、アプリケーションデータ、証明書撤去に関する
確認事項は[CaptureView MSIXパッケージのテスト](msix-testing.md)を参照してください。

## 自分でビルドしたEXEをWindowsがブロックする場合

最初に、自分で内容を確認したソースから作ったEXEであることを確認してください。
出所不明のバイナリに対して、セキュリティ警告を回避してはいけません。

### 1. Mark of the Webを確認

ダウンロードしたZIPなどを経由した場合、Zone.Identifierが引き継がれることが
あります。確認済みのローカルビルドだけを対象に実行します。

```powershell
Get-Item .\build\CaptureView.exe | Unblock-File
```

Visual StudioのReleaseビルドでは`.\build\Release\CaptureView.exe`に読み替えて
ください。`Unblock-File`が除去するのはダウンロード元情報だけで、Smart App
Control、Windows Defender Application Control（WDAC）、組織ポリシーを回避する
ものではありません。

### 2. Code Integrityログを確認

Device Guardまたは組織のポリシーによるブロックと表示された場合は、直近の
Code Integrityイベントを確認します。

```powershell
Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" -MaxEvents 20 |
  Select-Object TimeCreated, Id, Message |
  Format-List
```

イベント3077や3033などから、対象ファイルとポリシーを確認できます。会社・学校
管理のPCでは、ポリシーを変更せず管理者へ相談してください。

### 3. 開発PCのSmart App Control

Smart App Controlはビルド直後の無署名EXEをブロックする場合があり、アプリ単位の
例外指定はありません。個人管理の開発PCでは、必要性と保護低下を理解したうえで
次の画面から無効化する選択肢があります。

```text
Windows セキュリティ
  > アプリとブラウザー コントロール
  > スマート アプリ コントロールの設定
```

Microsoft Defenderは有効なままにしてください。また、これは一般利用者向けの
通常インストール手順として推奨するものではありません。最近のWindowsでは、
WindowsセキュリティからSmart App Controlを再度有効化できる場合があります。
現行仕様はMicrosoftの
[Smart App Control FAQ](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/smart-app-control-frequently-asked-questions)
を参照してください。

将来のMicrosoft Store版は、Microsoftが署名するMSIXパッケージとして配布する
予定です。開発用・ポータブル版は、発行者が信頼されたコード署名証明書を適用
しない限り無署名です。
