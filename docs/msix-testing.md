# Testing the CaptureView MSIX package

[日本語](#japanese)

This procedure is for local development only. Microsoft signs the package
distributed through the Store. Never distribute a package signed with the
self-signed test certificate described here.

## Summary

1. Build Release and create the MSIX.
2. Create a self-signed code-signing certificate.
3. Trust its public certificate under Local Machine `TrustedPeople`.
4. Sign, verify, install, and test the MSIX.
5. Uninstall the package and remove every copy of the test certificate.

The certificate subject must exactly match the Partner Center Publisher value.
Use private release-management records or repository secrets for the real
identity values; do not add them to this document.

For the detailed commands and verification checklist, see the private release
procedure maintained by the project owner. Important implementation findings:

- AppX deployment checks `LocalMachine\TrustedPeople`; importing only to a
  Current User store can fail with `0x800B0109`.
- Do not add a self-signed end-entity certificate to a Trusted Root store.
- The Store build requests camera access when video capture is first used and
  microphone access when audio monitoring is started.
- Packaged CaptureView continues to store settings and logs under
  `%LOCALAPPDATA%\CaptureView\`, shared with the portable build.
- Uninstalling the MSIX leaves this application-data directory in place.

---

<a id="japanese"></a>

# CaptureView MSIXパッケージのテスト

この手順はローカル開発専用です。Storeで配布されるパッケージはMicrosoftが署名
します。ここで説明する自己署名テスト証明書で署名したパッケージを配布しないで
ください。

## 概要

1. ReleaseをビルドしてMSIXを生成します。
2. 自己署名コード署名証明書を作成します。
3. 公開証明書をローカルコンピューターの`TrustedPeople`で信頼します。
4. MSIXを署名、検証、インストールして動作確認します。
5. パッケージをアンインストールし、テスト証明書をすべて削除します。

証明書のSubjectはPartner CenterのPublisher値と完全一致させる必要があります。
実際の製品IDはprivateなリリース管理記録またはRepository secretsから使用し、
この文書へ追加しないでください。

詳細なコマンドと確認項目は、プロジェクト所有者が管理するprivateなリリース
手順を参照してください。今回確認された重要事項は次のとおりです。

- AppX展開は`LocalMachine\TrustedPeople`を確認します。Current Userストアだけに
  登録した場合、`0x800B0109`で失敗することがあります。
- 自己署名したエンドエンティティ証明書を信頼されたルートストアへ追加しないで
  ください。
- Store版では、映像取得の初回利用時にカメラ、音声モニタリング開始時にマイク
  のアクセス許可を求めます。
- MSIX版もポータブル版と共有する`%LOCALAPPDATA%\CaptureView\`へ設定とログを
  保存します。
- MSIXをアンインストールしても、このアプリケーションデータは残ります。
