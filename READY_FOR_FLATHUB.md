# 🚀 Liftoff - Ready for Flathub! (Almost)

## ✅ What's Been Done

Your app has been updated with all the necessary Flathub requirements:

### Files Updated:
1. **Flatpak Manifest** (`lv.martinsz.Liftoff.json`)
   - ✅ Changed from local git path to GitHub repository
   - ✅ Set up for tag-based releases
   - ⚠️ Needs actual commit hash before submission

2. **MetaInfo File** (`data/lv.martinsz.Liftoff.metainfo.xml.in`)
   - ✅ Real app description added
   - ✅ Developer info (Martins Zeltins)
   - ✅ URLs pointing to GitHub repo
   - ✅ Release information for v1.0.0
   - ✅ Proper summary and features list

3. **Desktop File** (`data/lv.martinsz.Liftoff.desktop.in`)
   - ✅ Updated with proper name and metadata
   - ✅ Better categories and keywords

4. **README.md**
   - ✅ Complete project description
   - ✅ Installation instructions
   - ✅ Features list

5. **Icons**
   - ✅ Already have SVG icons in proper locations
   - ✅ Properly named: `lv.martinsz.Liftoff.svg`

## ⚠️ IMPORTANT: Before Submitting

### 1. App ID Decision (CRITICAL!)

**Current ID:** `lv.martinsz.Liftoff`
- Requires ownership of domain `martinsz.lv`

**If you DON'T own martinsz.lv:**
```bash
./convert-to-github-id.sh
```
This will convert everything to `io.github.martinszeltins.Liftoff` (recommended!)

### 2. Add Screenshots (REQUIRED!)

Take screenshots of your app and add them:
```bash
# Run your app and take screenshots
# Save to: screenshots/screenshot1.png
# Minimum 1 screenshot required
```

### 3. Create Git Tag and Update Manifest

```bash
# Commit all changes first
git add -A
git commit -m "Prepare for Flathub submission"
git push

# Create and push tag
git tag v1.0.0
git push origin v1.0.0

# Get the commit hash
git rev-parse v1.0.0
# Copy the hash (40 character string)
```

Then update `lv.martinsz.Liftoff.json` (or `io.github.martinszeltins.Liftoff.json` if you converted):
- Change `"tag": "1.0.0"` to `"tag": "v1.0.0"`
- Replace `"commit": "COMMIT_HASH_PLACEHOLDER"` with the actual hash

### 4. Test Locally

```bash
# Install flatpak-builder
flatpak install flathub org.flatpak.Builder

# Build and install
flatpak run --command=flatpak-builder org.flatpak.Builder --install lv.martinsz.Liftoff.json

# Test the app
flatpak run lv.martinsz.Liftoff
```

### 5. Run Linter

```bash
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest lv.martinsz.Liftoff.json
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

Fix any errors before submitting.

### 6. Submit to Flathub

```bash
# Fork flathub/flathub on GitHub (uncheck "Copy the master branch only")

# Clone your fork
git clone --branch=new-pr git@github.com:YOUR_USERNAME/flathub.git
cd flathub

# Create branch
git checkout -b liftoff-submission new-pr

# Copy only the manifest file
cp /home/martins/Programming/Liftoff/lv.martinsz.Liftoff.json .

# Commit and push
git add lv.martinsz.Liftoff.json
git commit -m "Add lv.martinsz.Liftoff"
git push origin liftoff-submission
```

Then create a Pull Request on GitHub:
- **Base branch:** `new-pr` (NOT master!)
- **Title:** "Add lv.martinsz.Liftoff"

## 📋 Quick Checklist

- [ ] Decide on App ID (keep lv.martinsz or convert to io.github)
- [ ] Add screenshots to screenshots/ directory
- [ ] Create git tag v1.0.0
- [ ] Update manifest with actual commit hash
- [ ] Commit and push all changes to GitHub
- [ ] Test build locally with flatpak-builder
- [ ] Run linter and fix issues
- [ ] Submit PR to flathub/flathub (new-pr branch)

## 📚 Additional Resources

- [FLATHUB_SUBMISSION.md](FLATHUB_SUBMISSION.md) - Detailed submission guide
- [APP_ID_DECISION.md](APP_ID_DECISION.md) - App ID explanation
- [convert-to-github-id.sh](convert-to-github-id.sh) - Script to switch to GitHub ID

## 🎉 After Approval

Once approved:
1. You'll get an invitation to `flathub/lv.martinsz.Liftoff` repository
2. Enable 2FA on GitHub (required)
3. Accept invitation within 1 week
4. Future updates go directly to that repo

---

**Need help?** Check the documentation or ask on:
- Matrix: #flathub:matrix.org
- Discourse: https://discourse.flathub.org/

Good luck with your submission! 🚀
