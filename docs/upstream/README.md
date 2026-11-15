# Drafts for MayaFlux

Not filed yet. MayaFlux's `CONTRIBUTING.md` requires every contribution to be authored, reviewed and submitted by a person, so read and edit each draft before filing it.

| File | Where | Title |
|---|---|---|
| `01-wayland-cursor-mode.md` | Issue | Wayland: honour InputConfig::cursor_mode (pointer lock and relative motion) |
| `02-modelreader-node-tree.md` | Issue | ModelReader: carry the Assimp node tree into MeshNetwork slots |
| `03-forma-set-focus.md` | Issue | Forma: set focus from code, so menus can be driven by keyboard or gamepad |
| `04-hid-gamepad-usages.md` | Issue | Input: bind HID controls by usage, with a standard gamepad profile |
| `discussion-game-on-nexus.md` | Discussion, category Ideas | A game on MayaFlux: Nexus as the entry point, Jolt for physics |

File the issues first, then put their links into the discussion's last section.

```sh
cd docs/upstream
gh issue create --repo MayaFlux/MayaFlux --title "Wayland: honour InputConfig::cursor_mode (pointer lock and relative motion)" --body-file 01-wayland-cursor-mode.md
gh issue create --repo MayaFlux/MayaFlux --title "ModelReader: carry the Assimp node tree into MeshNetwork slots" --body-file 02-modelreader-node-tree.md
gh issue create --repo MayaFlux/MayaFlux --title "Forma: set focus from code, so menus can be driven by keyboard or gamepad" --body-file 03-forma-set-focus.md
gh issue create --repo MayaFlux/MayaFlux --title "Input: bind HID controls by usage, with a standard gamepad profile" --body-file 04-hid-gamepad-usages.md

# Discussion in MayaFlux/MayaFlux, category Ideas (ids read 15 Nov 2026)
gh api graphql \
  -f query='mutation($repo:ID!,$cat:ID!,$title:String!,$body:String!){createDiscussion(input:{repositoryId:$repo,categoryId:$cat,title:$title,body:$body}){discussion{url}}}' \
  -f repo=R_kgDOOdFqvg -f cat=DIC_kwDOOdFqvs4Cxq-S \
  -f title="A game on MayaFlux: Nexus as the entry point, Jolt for physics" \
  -F body=@discussion-game-on-nexus.md
```
