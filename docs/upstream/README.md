# Requests to MayaFlux

Filed from these files, on 15 Nov 2026 unless the row says otherwise. Edits made on GitHub after that are not copied back here.

| File | Filed as |
|---|---|
| `01-wayland-cursor-mode.md` | [#188](https://github.com/MayaFlux/MayaFlux/issues/188) Wayland: honour InputConfig::cursor_mode (pointer lock and relative motion) |
| `02-modelreader-node-tree.md` | [#189](https://github.com/MayaFlux/MayaFlux/issues/189) ModelReader: carry the Assimp node tree into MeshNetwork slots |
| `03-forma-set-focus.md` | [#190](https://github.com/MayaFlux/MayaFlux/issues/190) Forma: set focus from code, so menus can be driven by keyboard or gamepad |
| `04-hid-gamepad-usages.md` | [#191](https://github.com/MayaFlux/MayaFlux/issues/191) Input: bind HID controls by usage, with a standard gamepad profile |
| `04-hid-gamepad-usages-comment.md` | [Comment on #191](https://github.com/MayaFlux/MayaFlux/issues/191#issuecomment-6899444834): TabletBackend already parses descriptors; Xbox pads over USB aren't HID on Linux; no longer needed for this game |
| `discussion-game-on-nexus.md` | [Discussion #192](https://github.com/orgs/MayaFlux/discussions/192), category Ideas. Retitled and edited on 15 Nov for tablet navigation: "A maze game on MayaFlux: Nexus as the entry point, navigated with a drawing tablet" |
| `pr-forma-set-focus.md` | [PR #193](https://github.com/MayaFlux/MayaFlux/pull/193) Forma: set keyboard focus from code (Context::set_focus). Closes #190. Branch `kdlydia/MayaFlux:forma-set-focus`, based on main. Description updated on 16 Nov for the second commit |
| `pr-forma-set-focus-reply-1.md` | [Reply on #193](https://github.com/MayaFlux/MayaFlux/pull/193#issuecomment-6921023278) to the 16 Nov review (changes requested: focus callbacks that move focus). Fixed in `b70fab2a` |
| `05-phase2-requests.md` | [#197](https://github.com/MayaFlux/MayaFlux/issues/197) Part 2 of #196: requests from a maze game (a Locus that plays file cameras, timed weight fades, named markers). Filed on 24 Nov; follow-up comment with items 6–11 on 25 Nov. Closed by [#198](https://github.com/MayaFlux/MayaFlux/pull/198) on 26 Nov; items 9 and 11 split out as asked, reply on 26 Nov |
| `06-raymarch-scene-depth.md` | [#199](https://github.com/MayaFlux/MayaFlux/issues/199) RaymarchBuffer: stop the march at scene depth. #197 item 9, filed on 26 Nov |
| `07-flow-solid-cells.md` | [#200](https://github.com/MayaFlux/MayaFlux/issues/200) Volume flow: solid cells inside the lattice. #197 item 11, filed on 26 Nov |

To follow them:

```sh
for n in 188 189 190 191 197 199 200; do gh issue view $n --repo MayaFlux/MayaFlux --json number,state,comments --jq '"\(.number) \(.state) comments=\(.comments|length)"'; done
gh api graphql -f query='{ repository(owner:"MayaFlux", name:"MayaFlux"){ discussion(number:192){ comments(first:20){ totalCount nodes{ author{login} body } } } } }'
```
