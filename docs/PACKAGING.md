# Windows Packaging Strategy

## Overview

MoonLit will be distributed as a Windows installer (.exe) using NSIS as the primary installer. A Microsoft Store version (MSIX) is planned for future release when a developer account becomes available.

## Distribution Formats

### Primary: NSIS Installer (.exe)
- **Format**: Executable installer
- **Target**: Windows 10 1903+ and Windows 11 x86_64
- **Installation**: Current user (no admin required)
- **Priority**: High (immediate)

### Secondary: MSIX (Microsoft Store)
- **Format**: MSIX package
- **Target**: Windows 10+ and Windows 11
- **Installation**: Microsoft Store
- **Priority**: Low (future, requires developer account)

### Future: Portable Version
- **Format**: ZIP archive with executable
- **Target**: Windows 10+ and Windows 11
- **Installation**: No installation required
- **Priority**: Medium (if requested)

## NSIS Installer Configuration

### Installation Requirements
- No administrator privileges required
- Current user installation
- Automatic WebView2 installation (if needed)
- Create Start Menu shortcuts
- Create Desktop shortcut (optional)
- Create Uninstall entry

### Installation Paths
```
Default: %LOCALAPPDATA%\MoonLit
Executables: %LOCALAPPDATA%\MoonLit\MoonLit.exe
Data: %APPDATA%\com.souriscg.moonlit
```

### Installer Features
- Customizable installation directory
- Optional Desktop shortcut
- Optional Start Menu shortcut
- Silent installation mode
- Uninstaller with clean removal

### Installer Assets
- Installer icon (icon.ico)
- Uninstaller icon (icon.ico)
- Installer banner (164x314 bitmap)
- Installer wizard (164x314 bitmap)
- License agreement (GPL-3.0)

### Installer Script Structure
```nsis
!define PRODUCT_NAME "MoonLit"
!define PRODUCT_VERSION "0.1.0"
!define PRODUCT_PUBLISHER "SourisCG"
!define PRODUCT_WEB_SITE "https://github.com/SourisCG/MoonLit"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "MoonLit_${PRODUCT_VERSION}_x64-setup.exe"
InstallDir "$LOCALAPPDATA\${PRODUCT_NAME}"
RequestExecutionLevel user

Section "Install"
  ; Install files
  ; Create shortcuts
  ; Register uninstaller
SectionEnd

Section "Uninstall"
  ; Remove files
  ; Remove shortcuts
  ; Remove registry entries
SectionEnd
```

## Tauri Bundle Configuration

### Current Configuration (tauri.conf.json)
```json
{
  "bundle": {
    "active": true,
    "targets": ["nsis"],
    "icon": [
      "icons/32x32.png",
      "icons/128x128.png",
      "icons/128x128@2x.png",
      "icons/icon.ico"
    ],
    "windows": {
      "nsis": {
        "installMode": "currentUser",
        "languages": ["English", "Spanish"],
        "displayLanguageSelector": true
      }
    }
  }
}
```

### Build Command
```bash
npm run tauri build
```

### Output
```
src-tauri/target/release/bundle/nsis/MoonLit_0.1.0_x64-setup.exe
```

## Microsoft Store (Future)

### Requirements
- Microsoft Store Developer Account ($19 one-time)
- MSIX packaging
- Code signing certificate
- Store listing assets
- Privacy policy

### MSIX Configuration
```json
{
  "bundle": {
    "targets": ["msi", "nsis"],
    "windows": {
      "msi": {
        "upgradeCode": "GUID-HERE"
      }
    }
  }
}
```

### Store Limitations
- More restrictive sandbox
- May limit some Windows API access
- Automatic updates through Store
- Requires regular updates to maintain listing

## Code Signing (Optional)

### Self-Signed Certificate (Development)
```powershell
# Create self-signed certificate
New-SelfSignedCertificate -Type Custom -Subject "CN=MoonLit, O=SourisCG" -KeyUsage DigitalSignature -FriendlyName "MoonLit Code Signing" -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

# Export certificate
$cert = Get-ChildItem "Cert:\CurrentUser\My\<thumbprint>"
Export-PfxCertificate -Cert $cert -FilePath "certificate.pfx" -Password (ConvertTo-SecureString -String "password" -Force -AsPlainText)
```

### Commercial Certificate (Production)
- Purchase from Certificate Authority (DigiCert, Sectigo, etc.)
- Cost: $200-$400/year
- Required for Microsoft Store
- Prevents "Unknown Publisher" warnings

### Signing Process
```bash
# Sign executable
signtool sign /f certificate.pfx /p password /tr http://timestamp.digicert.com /td sha256 /fd sha256 MoonLit.exe

# Sign installer
signtool sign /f certificate.pfx /p password /tr http://timestamp.digicert.com /td sha256 /fd sha256 MoonLit-setup.exe
```

## Release Process

### Development Release
1. Update version in `package.json` and `tauri.conf.json`
2. Update `Cargo.toml` version
3. Run automated tests
4. Build installer: `npm run tauri build`
5. Test installer on clean Windows
6. Create GitHub release with installer

### Production Release
1. Update version and changelog
2. Run all automated tests
3. Run manual tests on Windows
4. Build signed installer
5. Test signed installer
6. Create GitHub release
7. Upload to Microsoft Store (future)
8. Announce release

### Version Numbering
- **Format**: MAJOR.MINOR.PATCH
- **MAJOR**: Breaking changes
- **MINOR**: New features (backward compatible)
- **PATCH**: Bug fixes

Example: `0.1.0` → `0.2.0` → `1.0.0`

## Assets Required

### Icons
- [ ] Application icon (ICO format, multiple sizes)
- [ ] Installer icon (ICO format)
- [ ] Uninstaller icon (ICO format)
- [ ] Store icon (PNG, 300x300)
- [ ] Favicon (ICO format)

### Installer Images
- [ ] Banner image (164x314 bitmap)
- [ ] Wizard image (164x314 bitmap)
- [ ] Welcome image (optional)

### Store Assets (Future)
- [ ] Store logo (PNG, multiple sizes)
- [ ] Screenshots (multiple, 1920x1080)
- [ ] Description (English)
- [ ] Keywords
- [ ] Privacy policy
- [ ] Support information

## File Structure

### Installed Files
```
%LOCALAPPDATA%\MoonLit\
├── MoonLit.exe                 # Main executable
├── WebView2Loader.dll          # WebView2 runtime
├── resources/                  # Application resources
│   ├── icons/                  # Application icons
│   └── config/                 # Default configuration
└── uninstall.exe               # Uninstaller
```

### User Data
```
%APPDATA%\com.souriscg.moonlit\
├── config.json                 # User configuration
├── library.db                  # SQLite database
├── logs/                       # Log files
└── temp/                       # Temporary files
```

### Clip Storage
```
%USERPROFILE%\Videos\MoonLit\
├── 2026-07-23_14-30-45.mp4    # Clip files
├── 2026-07-23_14-35-12.mp4
└── ...
```

## Update Mechanism

### Manual Updates (Current)
1. Check GitHub releases page
2. Download new installer
3. Run installer (overwrites previous version)
4. Configuration and library preserved

### Automatic Updates (Future)
- Check for updates on startup
- Download update in background
- Install update on next restart
- Preserve configuration and library

### Update Notification
- Toast notification when update available
- Link to download page
- Optional: automatic download

## Uninstallation

### Uninstaller Features
- Remove all installed files
- Remove Start Menu shortcuts
- Remove Desktop shortcuts (if created)
- Remove registry entries
- Optional: Keep user data
- Optional: Delete clips

### Uninstaller Script
```nsis
Section "Uninstall"
  ; Remove files
  RMDir /r "$INSTDIR"
  
  ; Remove shortcuts
  Delete "$SMPROGRAMS\MoonLit.lnk"
  Delete "$DESKTOP\MoonLit.lnk"
  
  ; Remove registry
  DeleteRegKey HKCU "Software\SourisCG\MoonLit"
  
  ; Ask to delete user data
  MessageBox MB_YESNO "Delete user data and clips?" IDYES deleteData IDNO keepData
  deleteData:
    RMDir /r "$APPDATA\com.souriscg.moonlit"
    RMDir /r "$USERPROFILE\Videos\MoonLit"
  keepData:
SectionEnd
```

## Dependencies

### WebView2 Runtime
- **Requirement**: WebView2 runtime must be installed
- **Windows 10/11**: Usually pre-installed
- **Installation**: Automatic if missing (NSIS can install)
- **Size**: ~180 MB

### GPU Drivers (Optional)
- **NVIDIA**: Latest drivers for NVENC
- **AMD**: Latest drivers for AMF
- **Intel**: Latest drivers for QuickSync
- **Note**: Software encoding works without GPU drivers

### Windows Runtime
- **Requirement**: Windows 10 1903+ or Windows 11
- **Runtime**: Built into Windows
- **No additional installation required**

## Security Considerations

### Code Signing
- Sign all executables and DLLs
- Sign installer
- Prevents "Unknown Publisher" warnings
- Required for Microsoft Store

### Permissions
- No administrator privileges required
- Current user installation only
- No system-wide changes
- No service installation

### Privacy
- No data collection
- No telemetry
- No network requests (except for updates)
- Local storage only

### Sandboxing (Future - Microsoft Store)
- MSIX sandboxing
- Limited file system access
- Limited registry access
- Limited network access

## Troubleshooting

### Common Issues

#### "Unknown Publisher" Warning
- **Cause**: Unsigned installer
- **Solution**: Sign installer with code signing certificate
- **Workaround**: Click "More info" → "Run anyway"

#### WebView2 Not Installed
- **Cause**: Old Windows 10 version
- **Solution**: Installer will install WebView2 automatically
- **Manual**: Download from https://developer.microsoft.com/en-us/microsoft-edge/webview2/

#### Installation Blocked by Antivirus
- **Cause**: Unsigned or new software
- **Solution**: Add to antivirus exclusions or whitelist
- **Long-term**: Sign installer with commercial certificate

#### Cannot Uninstall
- **Cause**: Files in use or permissions
- **Solution**: Close MoonLit, run as administrator
- **Manual**: Delete installation folder

## References

- [Tauri Bundler Documentation](https://v2.tauri.app/distribute/)
- [NSIS Documentation](https://nsis.sourceforge.io/Docs/)
- [Microsoft Store Documentation](https://docs.microsoft.com/windows/uwp/publish/)
- [Code Signing Best Practices](https://docs.microsoft.com/windows-hardware/drivers/dashboard/code-signing-best-practices)
- [WebView2 Distribution](https://docs.microsoft.com/microsoft-edge/webview2/concepts/distribution)
