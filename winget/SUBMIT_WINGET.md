# Winget Publishing Guide

## Prerequisites

1. Fork `microsoft/winget-pkgs` repository
2. Git installed
3. Package built và release đã được publish trên GitHub

## Steps

### 1. Calculate SHA256 của installer

```powershell
# Calculate SHA256 for your release zip
Get-FileHash -Algorithm SHA256 -Path ttyd-win.x64.zip
```

### 2. Update manifest files

Edit các file trong `manifests/t/pxuanbach/ttyd/`:
- Thay `1.7.7` bằng version hiện tại
- Thay `REPLACE_WITH_SHA256_OF_ZIP` bằng SHA256 đã tính
- Thay `REPLACE_WITH_SHA256_OF_ZIP` bằng SHA256 đã tính

### 3. Test locally (optional)

```powershell
# Validate manifest
winget validate --manifest manifests\t\pxuanbach\ttyd\

# Install locally to test
winget install --manifest manifests\t\pxuanbach\ttyd\ --verbose
```

### 4. Submit Pull Request

```bash
# Clone your fork
git clone https://github.com/pxuanbach/winget-pkgs.git
cd winget-pkgs

# Create branch
git checkout -b add-ttyd-1.7.7

# Copy manifest files
mkdir -p manifests/t/pxuanbach/ttyd/1.7.7
cp /path/to/winget/* manifests/t/pxuanbach/ttyd/1.7.7/

# Commit và push
git add .
git commit -m "Add ttyd version 1.7.7"
git push origin add-ttyd-1.7.7

# Open PR at https://github.com/microsoft/winget-pkgs/compare
```

### 5. Wait for validation

Microsoft's automated pipeline sẽ:
- Validate YAML syntax
- Download và verify installer
- Scan for malware
- Build singleton manifest

Thời gian: **1-3 ngày làm việc**

## File Structure

```
manifests/
└── t/
    └── pxuanbach/
        └── ttyd/
            └── 1.7.7/
                ├── ttyd.yaml
                ├── ttyd.installer.yaml
                └── ttyd.locale.en-US.yaml
```

## References

- [Winget-PKGs Contribution Guide](https://github.com/microsoft/winget-pkgs/blob/master/CONTRIBUTING.md)
- [Manifest Specification](https://github.com/microsoft/winget-cli/blob/master/doc/ManifestSpecv1.0.md)
