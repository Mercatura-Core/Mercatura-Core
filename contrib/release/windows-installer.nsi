# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
# STAGE and OUTPUT are supplied by the release packager; no production datadir.
Unicode true
Name "Mercatura Core v0.1.2 Public Testnet"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Mercatura Core Testnet"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion 0.1.2.0
VIAddVersionKey ProductName "Mercatura Core Public Testnet"
VIAddVersionKey FileVersion "0.1.2"
VIAddVersionKey FileDescription "Mercatura Core Public Testnet installer"
VIAddVersionKey LegalCopyright "The Mercatura Core developers"
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Section
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  CreateShortcut "$SMPROGRAMS\Mercatura Core Testnet.lnk" "$INSTDIR\mercatura-qt.exe" "-testnet"
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd
Section "Uninstall"
  # Remove only distributed files; never recursively remove a wallet/datadir.
  !include "${REMOVE_FILES}"
  Delete "$SMPROGRAMS\Mercatura Core Testnet.lnk"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
