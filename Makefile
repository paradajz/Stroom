PRESET     ?= stroom
VIDEO_MODE ?= 480i

ifneq ($(VIDEO_MODE),480p)
ifneq ($(VIDEO_MODE),480i)
$(error Unknown VIDEO_MODE: $(VIDEO_MODE); use 480p or 480i)
endif
endif

# CMake builds the app and tests; PS2Link retains its upstream build.
.DEFAULT_GOAL := stroom
CMAKE         ?= cmake
CTEST         ?= ctest
CLANG_FORMAT  ?= clang-format
CLANG_TIDY    ?= clang-tidy
PRETTIER      ?= prettier

# Shared build roots are fixed so cleaning cannot overlap an SDK installation.
override BUILD_DIR_BASE      := $(abspath build)
override BUILD_DIR_SDK       := $(BUILD_DIR_BASE)/sdk
override BUILD_DIR_INSTALLER := $(BUILD_DIR_BASE)/installer
override BUILD_DIR_CONFIG    := $(BUILD_DIR_BASE)/$(PRESET)/$(VIDEO_MODE)

BUILD_DIR_APP         := $(BUILD_DIR_CONFIG)/app
BUILD_DIR_TESTS       := $(BUILD_DIR_CONFIG)/tests
BUILD_DIR_LINT        := $(BUILD_DIR_CONFIG)/lint
BUILD_DIR_DIAGNOSTICS := $(BUILD_DIR_CONFIG)/diagnostics
BUILD_DIR_PS2LINK     := $(BUILD_DIR_CONFIG)/ps2link
PS2LINK_SOURCE        ?= third_party/ps2link

# Select the SDK matching the app preset.
override SDK_VARIANT := $(if $(filter stroom,$(PRESET)),release,diagnostic)

SDK_ENVIRONMENT       := $(BUILD_DIR_SDK)/$(SDK_VARIANT)/environment.sh
SDK_BUILT_MARKER      := $(BUILD_DIR_SDK)/$(SDK_VARIANT)/built

.PHONY: all
.PHONY: submodules
.PHONY: sdk
.PHONY: prod
.PHONY: dev
.PHONY: installer
.PHONY: cd-listen
.PHONY: install-cd-listener
.PHONY: uninstall-cd-listener
.PHONY: build-cd-listener
.PHONY: test-cd-listener
.PHONY: cd-recognize
.PHONY: install-cd-recognition
.PHONY: uninstall-cd-recognition
.PHONY: stroom
.PHONY: app
.PHONY: configure-app
.PHONY: configure-stroom
.PHONY: configure-tests
.PHONY: run
.PHONY: reset
.PHONY: test
.PHONY: lint
.PHONY: lint-cd-listener
.PHONY: lint-release
.PHONY: lint-diagnostics
.PHONY: lint-benchmark
.PHONY: lint-benchmark-quick
.PHONY: lint-configuration
.PHONY: format
.PHONY: clean
.PHONY: clean-all
.PHONY: diagnostics
.PHONY: ps2link
.PHONY: diagnostic
.PHONY: diagnostic-clear
.PHONY: benchmark
.PHONY: benchmark-quick
.PHONY: benchmark-run

all: stroom

prod: app
	@node tools/setup/prepare.mjs prod "$(BUILD_DIR_APP)/stroom-packed.elf"

dev: ps2link
	@node tools/setup/prepare.mjs dev "$(BUILD_DIR_PS2LINK)/PS2LINK.ELF" "$(PS2LINK_SOURCE)"

# One-time signed memory-card bootloader installation for unmodded consoles.
installer:
	@node tools/setup/installer.mjs "$(BUILD_DIR_INSTALLER)"

# Run these host tools on the computer receiving audio or supplying metadata.
build-cd-listener:
	@$(CMAKE) -S tools/cd/listener -B "$(BUILD_DIR_BASE)/cd-listener" -DCMAKE_BUILD_TYPE=Release
	@$(CMAKE) --build "$(BUILD_DIR_BASE)/cd-listener" --target stroom-cd-listener --parallel

cd-listen: build-cd-listener
	@"$(BUILD_DIR_BASE)/cd-listener/stroom-cd-listener" $(if $(CONFIG),--config "$(CONFIG)") $(ARGS)

install-cd-listener: build-cd-listener
	@node tools/cd/listener/install.mjs install "$(BUILD_DIR_BASE)/cd-listener/stroom-cd-listener" $(if $(CONFIG),--config "$(CONFIG)") $(ARGS)

uninstall-cd-listener:
	@node tools/cd/listener/install.mjs uninstall

test-cd-listener: build-cd-listener
	@$(CMAKE) --build "$(BUILD_DIR_BASE)/cd-listener" --parallel
	@$(CTEST) --test-dir "$(BUILD_DIR_BASE)/cd-listener" --output-on-failure

cd-recognize install-cd-recognition: export STROOM_CD_CONFIG := $(CONFIG)
cd-recognize:
	@node tools/cd/recognition/service.mjs

install-cd-recognition:
	@node tools/cd/recognition/install.mjs install

uninstall-cd-recognition:
	@node tools/cd/recognition/install.mjs uninstall

submodules:
	@echo "Updating submodules"
	@git submodule update --init --recursive

sdk: submodules $(SDK_BUILT_MARKER)

$(SDK_BUILT_MARKER): | submodules
	@echo "Building $(SDK_VARIANT) SDK"
	@sh tools/sdk/build.sh --build-dir "$(BUILD_DIR_SDK)" --variant "$(SDK_VARIANT)" --built-marker "$(SDK_BUILT_MARKER)"

configure-stroom: configure-app

configure-app: sdk
	@echo "Configuring $(PRESET)"
	@. "$(SDK_ENVIRONMENT)" && \
	CLANG_TIDY="$(CLANG_TIDY)" $(CMAKE) -S src --preset $(PRESET) -B "$(BUILD_DIR_APP)" -DSTROOM_VIDEO_MODE="$(VIDEO_MODE)" $(if $(PS2_IP),-DPS2_IP="$(PS2_IP)")

stroom: app

app: configure-app
	@echo "Building $(PRESET)"
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)"

run reset: configure-app
	@echo "$(if $(filter reset,$@),Resetting console,Launching $(PRESET))"
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)" --target $@

configure-tests:
	@echo "Configuring tests"
	@$(CMAKE) -S tests -G "Unix Makefiles" -B "$(BUILD_DIR_TESTS)" -DSTROOM_VIDEO_MODE="$(VIDEO_MODE)"

test: configure-tests
	@echo "Building tests"
	@$(CMAKE) --build "$(BUILD_DIR_TESTS)"
	@echo "Running tests"
	@$(CTEST) --test-dir "$(BUILD_DIR_TESTS)" --output-on-failure

lint: lint-release lint-diagnostics lint-benchmark lint-benchmark-quick lint-cd-listener

lint-cd-listener: build-cd-listener
	@$(CLANG_TIDY) -p "$(BUILD_DIR_BASE)/cd-listener" tools/cd/listener/main.cpp tools/cd/listener/network.cpp tools/cd/listener/playback.cpp tools/cd/listener/protocol.cpp tools/cd/listener/realtime.cpp tools/cd/listener/config.cpp

lint-release:
	@echo "Checking release configuration"
	@$(MAKE) lint-configuration PRESET=stroom

lint-diagnostics:
	@echo "Checking diagnostic configuration"
	@$(MAKE) lint-configuration PRESET=stroom-diagnostics

lint-benchmark:
	@echo "Checking benchmark configuration"
	@$(MAKE) lint-configuration PRESET=benchmark-full

lint-benchmark-quick:
	@echo "Checking quick benchmark configuration"
	@$(MAKE) lint-configuration PRESET=benchmark-quick

lint-configuration: BUILD_DIR_APP = $(BUILD_DIR_LINT)/app
lint-configuration: BUILD_DIR_TESTS = $(BUILD_DIR_LINT)/tests
lint-configuration: configure-app configure-tests
	@echo "Preparing lint inputs"
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)" --target compile-presets
	@$(CMAKE) --build "$(BUILD_DIR_TESTS)" --target compile-presets
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)" --target lint-iop
	@echo "Running clang-tidy"
	@CLANG_TIDY="$(CLANG_TIDY)" node tools/lint/lint.mjs "$(BUILD_DIR_APP)" "$(BUILD_DIR_TESTS)" "$(BUILD_DIR_LINT)"

format:
	@echo "Formatting"
	@CLANG_FORMAT="$(CLANG_FORMAT)" PRETTIER="$(PRETTIER)" node tools/formatting/format.mjs "$(BUILD_DIR_BASE)" "$(BUILD_DIR_APP)" "$(BUILD_DIR_TESTS)" "$(BUILD_DIR_LINT)"

# Keep SDK installations and the installer when cleaning application and test outputs.
clean:
	@echo "Cleaning"
	@for dir in "$(BUILD_DIR_BASE)"/*; do \
	    [ "$$dir" = "$(BUILD_DIR_SDK)" ] || [ "$$dir" = "$(BUILD_DIR_INSTALLER)" ] || rm -rf -- "$$dir"; \
	done

clean-all: clean
	@echo "Cleaning SDK installations and installer"
	@rm -rf -- "$(BUILD_DIR_SDK)" "$(BUILD_DIR_INSTALLER)"

diagnostics:
	@echo "Querying console diagnostics"
	@node tools/diagnostic/query.mjs "$(PS2_IP)" "$(DIAGNOSTIC)"

ps2link: sdk
	@echo "Building $(SDK_VARIANT) PS2Link"
	@sh tools/ps2link/build.sh --source-dir "$(PS2LINK_SOURCE)" --build-dir "$(BUILD_DIR_PS2LINK)" --sdk-dir "$(BUILD_DIR_SDK)" --variant "$(SDK_VARIANT)" --video-mode "$(VIDEO_MODE)"

diagnostic:
	@echo "Downloading diagnostic captures"
	@node tools/diagnostic/capture.mjs "$(PS2_IP)" "$(BUILD_DIR_DIAGNOSTICS)"

diagnostic-clear:
	@echo "Clearing diagnostic captures"
	@node tools/diagnostic/capture.mjs "$(PS2_IP)" "$(BUILD_DIR_DIAGNOSTICS)" clear

benchmark:
	@echo "Preparing benchmark"
	@$(MAKE) benchmark-run PRESET=benchmark-full

benchmark-quick:
	@echo "Preparing quick benchmark"
	@$(MAKE) benchmark-run PRESET=benchmark-quick

benchmark-run: app
	@echo "Running benchmark"
	@. "$(SDK_ENVIRONMENT)" && STROOM_VIDEO_MODE="$(VIDEO_MODE)" node tools/milkdrop/benchmark.mjs "$(PS2_IP)" "$(BUILD_DIR_APP)"
