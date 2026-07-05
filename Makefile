# Wave — build.
#
# Vendors the tree-sitter runtime and the C grammar on first build, compiles
# the core as a static lib, builds + runs the test binaries, and links the
# GUI application (`wave`) against GLFW + OpenGL.

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -Isrc -Ivendor -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src

# GLFW via Homebrew; OpenGL + Cocoa frameworks for the window/context.
# Link GLFW *statically* (the .a, by full path) so the shipped binary carries no
# dependency on a Homebrew dylib — the .app must run on machines without brew,
# and the hardened runtime's library validation rejects foreign-Team-ID dylibs.
GLFW_PREFIX := $(shell brew --prefix glfw 2>/dev/null)
GUI_CFLAGS  := -I$(GLFW_PREFIX)/include
GUI_LIBS    := $(GLFW_PREFIX)/lib/libglfw3.a \
               -framework OpenGL -framework Cocoa -framework IOKit \
               -framework CoreVideo -framework CoreFoundation \
               -framework CoreServices

VENDOR        := vendor
LSP_DIR       := $(VENDOR)/lsp
# The bundled TypeScript language server (+ its tsserver) — vendored so a shipped
# Wave has working TS/JS go-to-definition & diagnostics with no global install.
LSP_STAMP     := $(LSP_DIR)/node_modules/typescript-language-server/lib/cli.mjs

# Bundled ripgrep — vendored so project-wide content search (Cmd-Shift-F) works
# out of the box. The prebuilt release for the host platform is downloaded and
# its `rg` binary dropped into vendor/rg/.
RG_DIR        := $(VENDOR)/rg
RG_BIN        := $(RG_DIR)/rg
RG_VERSION    := 14.1.1
RG_ARCH       := $(shell uname -m)
ifeq ($(RG_ARCH),arm64)
RG_TARGET     := aarch64-apple-darwin
else
RG_TARGET     := x86_64-apple-darwin
endif
RG_TARBALL    := ripgrep-$(RG_VERSION)-$(RG_TARGET).tar.gz
RG_URL        := https://github.com/BurntSushi/ripgrep/releases/download/$(RG_VERSION)/$(RG_TARBALL)
TS_DIR        := $(VENDOR)/tree-sitter
TS_C_DIR      := $(VENDOR)/tree-sitter-c
TS_JS_DIR     := $(VENDOR)/tree-sitter-javascript
TS_TS_DIR     := $(VENDOR)/tree-sitter-typescript
TS_REPO       := https://github.com/tree-sitter/tree-sitter.git
TS_C_REPO     := https://github.com/tree-sitter/tree-sitter-c.git
TS_JS_REPO    := https://github.com/tree-sitter/tree-sitter-javascript.git
TS_TS_REPO    := https://github.com/tree-sitter/tree-sitter-typescript.git
TS_TAG        := v0.22.6
TS_C_TAG      := v0.21.4
TS_JS_TAG     := v0.21.4
TS_TS_TAG     := v0.21.2

BUILD   := build
QUERY_DIR := queries
# Headless core (no GLFW/GL dependency) — also what the tests link against.
CORE_SRC := src/piece_table.c src/buffer.c src/highlight.c src/langs.c src/workspace.c src/lsp.c src/search.c src/config.c src/editor.c src/runtime.c src/lsp_manager.c src/palette.c src/project_search.c src/overlay.c src/popover.c src/theme.c src/watch.c src/command.c src/yank.c src/tabs.c src/mode.c src/diagnostics.c src/layout.c src/edit_command.c src/view.c src/text_view.c src/input.c src/updater.c
CORE_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))

# tree-sitter runtime is a single translation unit (lib.c includes the rest).
# Each language grammar = a parser.c (+ an external scanner.c where present).
TS_OBJ   := $(BUILD)/ts_lib.o $(BUILD)/ts_c_parser.o \
            $(BUILD)/ts_js_parser.o $(BUILD)/ts_js_scanner.o \
            $(BUILD)/ts_ts_parser.o $(BUILD)/ts_ts_scanner.o \
            $(BUILD)/ts_tsx_parser.o $(BUILD)/ts_tsx_scanner.o

# GUI-only objects (need GLFW/GL headers; mac.o is Objective-C / Cocoa).
GUI_OBJ := $(BUILD)/font.o $(BUILD)/render.o $(BUILD)/stb_impl.o \
           $(BUILD)/draw.o $(BUILD)/input_glfw.o $(BUILD)/main.o $(BUILD)/mac.o

TEST_LIBS := -framework CoreServices -framework CoreFoundation

TESTS    := test_piece_table test_buffer test_highlight test_langs test_workspace test_lsp test_search test_editor test_yank test_tabs test_mode test_command test_config test_diagnostics test_layout test_edit_command test_view test_overlay test_popover test_input test_runtime test_lsp_manager test_updater
TEST_BIN := $(addprefix $(BUILD)/,$(TESTS))

.PHONY: all app test clean vendor lsp rg distclean icon bundle dist \
        dmg notarize release-macos

# --- macOS packaging ----------------------------------------------------------
# Version stamped into the bundle + artifact name. Override on release:
#   make dist VERSION=0.1.3-alpha
VERSION  ?= 0.1.3-alpha
APP       := $(BUILD)/Wave.app
APP_BIN   := $(APP)/Contents/MacOS
APP_RES   := $(APP)/Contents/Resources
DIST      := $(BUILD)/Wave-$(VERSION)-macos.zip
DMG       := $(BUILD)/Wave-$(VERSION)-macos.dmg

# Signing + notarization (ZENIT GROUP LLC). CODESIGN_ID auto-selects the
# "Developer ID Application" identity if one is installed, else falls back to an
# ad-hoc signature (runnable locally, but Gatekeeper-blocked when downloaded).
# NOTARY_PROFILE is a stored notarytool credential (see `make notarize`).
TEAM_ID       := D7HN42D467
NOTARY_PROFILE ?= wave-notary
CODESIGN_ID   ?= $(shell security find-identity -v -p codesigning 2>/dev/null \
                   | awk -F'"' '/Developer ID Application/{print $$2; exit}')

all: $(BUILD)/libwave.a app

# `app` pulls in the bundled language server + ripgrep so a fresh build is
# shippable as a self-contained pack (the C binary + its vendored tools).
app: lsp rg $(BUILD)/wave

# Install the bundled TS/JS language server into vendor/lsp (prefers bun, the
# project's standard runtime; falls back to npm). Idempotent.
lsp: $(LSP_STAMP)

# Download + extract the prebuilt ripgrep into vendor/rg. Idempotent.
rg: $(RG_BIN)

$(RG_BIN):
	@echo "  RG    ripgrep $(RG_VERSION) ($(RG_TARGET))"
	@mkdir -p $(RG_DIR)
	@curl -fsSL $(RG_URL) -o $(RG_DIR)/$(RG_TARBALL)
	@tar -xzf $(RG_DIR)/$(RG_TARBALL) -C $(RG_DIR) --strip-components=1 \
	    ripgrep-$(RG_VERSION)-$(RG_TARGET)/rg
	@rm -f $(RG_DIR)/$(RG_TARBALL)
	@chmod +x $(RG_BIN)

$(LSP_STAMP): $(LSP_DIR)/package.json
	@echo "  LSP   typescript-language-server (vendoring into $(LSP_DIR))"
	@cd $(LSP_DIR) && ( command -v bun >/dev/null 2>&1 && bun install \
	    || npm install --no-audit --no-fund --loglevel=error )

vendor: $(TS_DIR)/lib/src/lib.c $(TS_C_DIR)/src/parser.c \
        $(TS_JS_DIR)/src/parser.c $(TS_TS_DIR)/typescript/src/parser.c

$(TS_DIR)/lib/src/lib.c:
	@echo "  GIT   tree-sitter $(TS_TAG)"
	@git clone --depth 1 --branch $(TS_TAG) $(TS_REPO) $(TS_DIR)

$(TS_C_DIR)/src/parser.c:
	@echo "  GIT   tree-sitter-c $(TS_C_TAG)"
	@git clone --depth 1 --branch $(TS_C_TAG) $(TS_C_REPO) $(TS_C_DIR)

$(TS_JS_DIR)/src/parser.c:
	@echo "  GIT   tree-sitter-javascript $(TS_JS_TAG)"
	@git clone --depth 1 --branch $(TS_JS_TAG) $(TS_JS_REPO) $(TS_JS_DIR)

$(TS_TS_DIR)/typescript/src/parser.c:
	@echo "  GIT   tree-sitter-typescript $(TS_TS_TAG)"
	@git clone --depth 1 --branch $(TS_TS_TAG) $(TS_TS_REPO) $(TS_TS_DIR)

$(BUILD):
	@mkdir -p $(BUILD)

# --- tree-sitter objects ---
$(BUILD)/ts_lib.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -c $(TS_DIR)/lib/src/lib.c -o $@

$(BUILD)/ts_c_parser.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_C_DIR)/src -c $(TS_C_DIR)/src/parser.c -o $@

$(BUILD)/ts_js_parser.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_JS_DIR)/src -c $(TS_JS_DIR)/src/parser.c -o $@
$(BUILD)/ts_js_scanner.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_JS_DIR)/src -c $(TS_JS_DIR)/src/scanner.c -o $@

$(BUILD)/ts_ts_parser.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_TS_DIR)/typescript/src -c $(TS_TS_DIR)/typescript/src/parser.c -o $@
$(BUILD)/ts_ts_scanner.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_TS_DIR)/typescript/src -c $(TS_TS_DIR)/typescript/src/scanner.c -o $@

$(BUILD)/ts_tsx_parser.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_TS_DIR)/tsx/src -c $(TS_TS_DIR)/tsx/src/parser.c -o $@
$(BUILD)/ts_tsx_scanner.o: vendor | $(BUILD)
	$(CC) $(CFLAGS) -I$(TS_TS_DIR)/tsx/src -c $(TS_TS_DIR)/tsx/src/scanner.c -o $@

# --- core objects ---
$(BUILD)/%.o: src/%.c | $(BUILD) vendor
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libwave.a: $(CORE_OBJ) $(TS_OBJ)
	@echo "  AR    $@"
	@ar rcs $@ $^

# --- GUI objects (extra include path for GLFW) ---
$(BUILD)/font.o: src/font.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/stb_impl.o: src/stb_impl.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@
$(BUILD)/render.o: src/render.c | $(BUILD)
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -c $< -o $@
$(BUILD)/input_glfw.o: src/input_glfw.c | $(BUILD)
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -c $< -o $@
$(BUILD)/main.o: src/main.c | $(BUILD) vendor
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -DWAVE_VERSION=\"$(VERSION)\" -c $< -o $@
$(BUILD)/mac.o: src/mac.m | $(BUILD)
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -fobjc-arc -c $< -o $@

$(BUILD)/wave: $(GUI_OBJ) $(BUILD)/libwave.a
	@echo "  LD    $@"
	@$(CC) $(CFLAGS) $(GUI_OBJ) $(BUILD)/libwave.a $(GUI_LIBS) -o $@

# --- tests ---
$(BUILD)/%: tests/%.c $(BUILD)/libwave.a
	$(CC) $(CFLAGS) $< $(BUILD)/libwave.a $(TEST_LIBS) -o $@

test: $(TEST_BIN)
	@echo "== running tests =="
	@for t in $(TEST_BIN); do echo "-> $$t"; $$t || exit 1; done
	@echo "== all tests passed =="

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(TS_DIR) $(TS_C_DIR) $(LSP_DIR)/node_modules $(RG_DIR)

# Regenerate packaging/wave.icns from the PIL master (needs python3 + Pillow).
# The .icns is committed, so this is only needed when the icon design changes.
icon:
	@echo "  ICON  packaging/wave.icns"
	@python3 packaging/icon.py packaging/icon-master.png
	@rm -rf $(BUILD)/wave.iconset && mkdir -p $(BUILD)/wave.iconset
	@for sz in 16 32 128 256 512; do \
	    sips -z $$sz $$sz packaging/icon-master.png \
	        --out $(BUILD)/wave.iconset/icon_$${sz}x$${sz}.png >/dev/null; \
	    d=$$((sz*2)); sips -z $$d $$d packaging/icon-master.png \
	        --out $(BUILD)/wave.iconset/icon_$${sz}x$${sz}@2x.png >/dev/null; \
	done
	@iconutil -c icns $(BUILD)/wave.iconset -o packaging/wave.icns

# Assemble Wave.app: binary in Contents/MacOS, vendored lsp+rg beside it (Wave
# resolves them relative to the executable), icon + Info.plist in place.
bundle: app
	@echo "  BUNDLE $(APP) ($(VERSION))"
	@rm -rf $(APP)
	@mkdir -p $(APP_BIN) $(APP_RES)
	@cp $(BUILD)/wave $(APP_BIN)/wave
	@# Vendored payload lives in Resources/ (the canonical spot for non-code
	@# files) — NOT in MacOS/, where codesign would treat each .js as nested code
	@# and refuse to sign. Wave resolves ../Resources/vendor at runtime.
	@mkdir -p $(APP_RES)/vendor/lsp $(APP_RES)/vendor/rg
	@cp -R $(LSP_DIR)/node_modules $(APP_RES)/vendor/lsp/
	@cp $(LSP_DIR)/package.json $(APP_RES)/vendor/lsp/
	@cp $(RG_BIN) $(APP_RES)/vendor/rg/rg
	@cp -R $(QUERY_DIR) $(APP_RES)/queries
	@# Drop the node_modules .bin symlink shims — unused at runtime (Wave spawns
	@# cli.mjs directly) and they break codesign's bundle seal.
	@rm -rf $(APP_RES)/vendor/lsp/node_modules/.bin
	@cp packaging/wave.icns $(APP_RES)/wave.icns
	@sed 's/__VERSION__/$(VERSION)/g' packaging/Info.plist.in > $(APP)/Contents/Info.plist
	@printf 'APPL????' > $(APP)/Contents/PkgInfo
	@touch $(APP)
	@# Sign inside-out. With a Developer ID identity, enable the hardened runtime
	@# + a secure timestamp (both required for notarization); otherwise ad-hoc.
	@if [ -n "$(CODESIGN_ID)" ]; then \
	    echo "  SIGN  $(CODESIGN_ID) (hardened runtime)"; \
	    codesign --force --options runtime --timestamp --sign "$(CODESIGN_ID)" $(APP_RES)/vendor/rg/rg; \
	    codesign --force --options runtime --timestamp --sign "$(CODESIGN_ID)" $(APP_BIN)/wave; \
	    codesign --force --options runtime --timestamp --sign "$(CODESIGN_ID)" $(APP); \
	else \
	    echo "  SIGN  ad-hoc (no Developer ID cert — set up notarization for downloads)"; \
	    codesign --force --deep --sign - $(APP) >/dev/null 2>&1 || true; \
	fi

# Zip the bundle for a GitHub release asset.
dist: bundle
	@echo "  DIST  $(DIST)"
	@rm -f $(DIST)
	@cd $(BUILD) && /usr/bin/ditto -c -k --keepParent Wave.app $(notdir $(DIST))
	@echo "  ->    $(DIST)"

# Submit the (Developer ID-signed) app to Apple's notary service and staple the
# ticket onto Wave.app, so a downloaded copy opens with no Gatekeeper warning —
# no right-click, no xattr. Requires:
#   1. A "Developer ID Application" cert (Xcode > Settings > Accounts >
#      ZENIT GROUP LLC > Manage Certificates > + > Developer ID Application).
#   2. A stored notarytool credential named $(NOTARY_PROFILE), one-time:
#        xcrun notarytool store-credentials $(NOTARY_PROFILE) \
#          --apple-id gerencia@gzenit.com --team-id $(TEAM_ID) \
#          --password <app-specific-password>      # appleid.apple.com > Sign-In & Security
notarize: bundle
	@if [ -z "$(CODESIGN_ID)" ]; then \
	    echo "ERROR: no Developer ID Application cert found — create one in Xcode first."; exit 1; \
	fi
	@echo "  NOTARIZE submitting $(APP) as $(NOTARY_PROFILE)"
	@/usr/bin/ditto -c -k --keepParent $(APP) $(BUILD)/notarize.zip
	@xcrun notarytool submit $(BUILD)/notarize.zip --keychain-profile $(NOTARY_PROFILE) --wait
	@xcrun stapler staple $(APP)
	@rm -f $(BUILD)/notarize.zip
	@echo "  ->    stapled $(APP)"

# Build a drag-to-Applications .dmg from the current Wave.app (run after bundle,
# or after notarize so the app inside is stapled).
dmg:
	@echo "  DMG   $(DMG)"
	@rm -rf $(BUILD)/dmgroot $(DMG)
	@mkdir -p $(BUILD)/dmgroot
	@cp -R $(APP) $(BUILD)/dmgroot/
	@ln -s /Applications $(BUILD)/dmgroot/Applications
	@hdiutil create -volname "Wave" -srcfolder $(BUILD)/dmgroot \
	    -ov -format UDZO $(DMG) >/dev/null
	@rm -rf $(BUILD)/dmgroot
	@echo "  ->    $(DMG)"

# Full notarized macOS release: build + sign + notarize + staple, then a .dmg
# whose embedded app is stapled (opens offline, zero warnings).
release-macos:
	@$(MAKE) bundle
	@$(MAKE) notarize
	@$(MAKE) dmg
	@echo "  DONE  $(DMG) (notarized + stapled)"
