# Flathub Submission Checklist

## Before Submitting to Flathub

### 1. **CRITICAL: Add Screenshots**
You need to take screenshots of your app and add them to the repository:
- Create at least one screenshot showing the app in action
- Save it as `screenshots/screenshot1.png`
- Commit and push to GitHub
- The MetaInfo file is already configured to look for this screenshot

### 2. **CRITICAL: Update Manifest with Git Tag and Commit**
Before submitting, you need to:
1. Create a git tag for version 1.0.0:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. Get the commit hash:
   ```bash
   git rev-parse v1.0.0
   ```

3. Update the manifest file `lv.martinsz.Liftoff.json`:
   - Replace `"tag": "1.0.0"` with `"tag": "v1.0.0"` (add the 'v' prefix)
   - Replace `"commit": "COMMIT_HASH_PLACEHOLDER"` with the actual commit hash

### 3. Test Locally Before Submission

Install the flatpak-builder tool:
```bash
flatpak install flathub org.flatpak.Builder
```

Add Flathub repo if not already added:
```bash
flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
```

Build your app:
```bash
flatpak run --command=flatpak-builder org.flatpak.Builder --install lv.martinsz.Liftoff.json
```

Test it:
```bash
flatpak run lv.martinsz.Liftoff
```

### 4. Run the Linter

```bash
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest lv.martinsz.Liftoff.json
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

Fix any errors before submitting.

### 5. Submit to Flathub

1. Fork the Flathub repository:
   - Go to https://github.com/flathub/flathub
   - Click Fork (UNCHECK "Copy the master branch only")

2. Clone your fork:
   ```bash
   git clone --branch=new-pr git@github.com:YOUR_USERNAME/flathub.git
   cd flathub
   ```

3. Create a branch:
   ```bash
   git checkout -b liftoff-submission new-pr
   ```

4. Add your files:
   ```bash
   # Copy only these files:
   cp /path/to/Liftoff/lv.martinsz.Liftoff.json .
   git add lv.martinsz.Liftoff.json
   git commit -m "Add lv.martinsz.Liftoff"
   git push origin liftoff-submission
   ```

5. Create Pull Request:
   - Go to your fork on GitHub
   - Click "Pull Request"
   - **IMPORTANT:** Set base branch to `new-pr` (NOT `master`)
   - Title: "Add lv.martinsz.Liftoff"
   - Submit!

### 6. During Review

- Respond to reviewer comments
- Make changes in your Liftoff repository (NOT in the flathub fork)
- Update the manifest if needed
- **DO NOT** close and reopen the PR
- **DO NOT** merge master into your branch

### 7. After Approval

- You'll receive an invitation to the flathub/lv.martinsz.Liftoff repository
- Enable 2FA on GitHub if you haven't already
- Accept the invitation within one week
- Future updates go to that new repository, not through pull requests

## Files Ready for Flathub

✅ Manifest: `lv.martinsz.Liftoff.json`
✅ MetaInfo: `data/lv.martinsz.Liftoff.metainfo.xml.in`
✅ Desktop File: `data/lv.martinsz.Liftoff.desktop.in`
✅ Icons: SVG icons in `data/icons/`
✅ License: GPL-2.0-or-later (in COPYING file)
✅ README: Updated with proper description

## TODO Before Submission

✅ **COMPLETED:** Screenshots added to `screenshots/` directory (screenshot1.png, 76 KB)
✅ **COMPLETED:** Git tag v1.0.0 created and pushed to GitHub
✅ **COMPLETED:** Manifest updated with actual commit hash (bc2dfc05962be4e4377dd71390ba162102e2eae1)
✅ **COMPLETED:** flathub.json created with permission exceptions
✅ **COMPLETED:** Linter run (2 expected errors documented in flathub.json)

**STATUS: ✅ READY FOR SUBMISSION!**

## Additional Notes

- ✅ The app ID `lv.martinsz.Liftoff` uses your domain martinsz.lv
- ✅ Domain verification: martinsz.lv is accessible via HTTPS (200 OK)
- ✅ You own/control the domain, so the app ID is valid
- ✅ No need to use GitHub-based ID

**All requirements satisfied! Ready to submit to Flathub.**
