PRESET ?= stroom

# CMake builds the app and tests; PS2Link retains its upstream build.
.DEFAULT_GOAL := stroom
CMAKE         ?= cmake
CTEST         ?= ctest
CLANG_FORMAT  ?= clang-format
CLANG_TIDY    ?= clang-tidy
PRETTIER      ?= prettier

# Build and SDK roots are fixed so cleaning cannot overlap an SDK installation.
override BUILD_DIR_BASE := $(abspath build)
override BUILD_DIR_SDK  := $(BUILD_DIR_BASE)/sdk

BUILD_DIR_APP         := $(BUILD_DIR_BASE)/$(PRESET)
BUILD_DIR_TESTS       := $(BUILD_DIR_BASE)/tests
BUILD_DIR_LINT        := $(BUILD_DIR_BASE)/lint
BUILD_DIR_DIAGNOSTICS := $(BUILD_DIR_BASE)/diagnostics
BUILD_DIR_PS2LINK     := $(BUILD_DIR_BASE)/ps2link
PS2LINK_SOURCE        ?= third_party/ps2link

# Select the SDK matching the app preset.
override SDK_VARIANT := $(if $(filter stroom,$(PRESET)),release,diagnostic)

SDK_ENVIRONMENT       := $(BUILD_DIR_SDK)/$(SDK_VARIANT)/environment.sh
SDK_BUILT_MARKER      := $(BUILD_DIR_SDK)/$(SDK_VARIANT)/built

.PHONY: all
.PHONY: submodules
.PHONY: sdk
.PHONY: stroom
.PHONY: app
.PHONY: configure-app
.PHONY: configure-stroom
.PHONY: configure-tests
.PHONY: run
.PHONY: reset
.PHONY: test
.PHONY: lint
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
	CLANG_TIDY="$(CLANG_TIDY)" $(CMAKE) -S src --preset $(PRESET) -B "$(BUILD_DIR_APP)" $(if $(PS2_IP),-DPS2_IP="$(PS2_IP)")

stroom: app

app: configure-app
	@echo "Building $(PRESET)"
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)"

run reset: configure-app
	@echo "$(if $(filter reset,$@),Resetting console,Launching $(PRESET))"
	@. "$(SDK_ENVIRONMENT)" && $(CMAKE) --build "$(BUILD_DIR_APP)" --target $@

configure-tests:
	@echo "Configuring tests"
	@$(CMAKE) -S tests -G "Unix Makefiles" -B "$(BUILD_DIR_TESTS)"

test: configure-tests
	@echo "Building tests"
	@$(CMAKE) --build "$(BUILD_DIR_TESTS)"
	@echo "Running tests"
	@$(CTEST) --test-dir "$(BUILD_DIR_TESTS)" --output-on-failure

lint: lint-release lint-diagnostics lint-benchmark lint-benchmark-quick

lint-release:
	@echo "Checking release configuration"
	@$(MAKE) lint-configuration PRESET=stroom BUILD_DIR_APP="$(BUILD_DIR_LINT)/release/app" BUILD_DIR_TESTS="$(BUILD_DIR_LINT)/release/tests" BUILD_DIR_LINT="$(BUILD_DIR_LINT)/release"

lint-diagnostics:
	@echo "Checking diagnostic configuration"
	@$(MAKE) lint-configuration PRESET=stroom-diagnostics BUILD_DIR_APP="$(BUILD_DIR_LINT)/diagnostics/app" BUILD_DIR_TESTS="$(BUILD_DIR_LINT)/diagnostics/tests" BUILD_DIR_LINT="$(BUILD_DIR_LINT)/diagnostics"

lint-benchmark:
	@echo "Checking benchmark configuration"
	@$(MAKE) lint-configuration PRESET=benchmark-full BUILD_DIR_APP="$(BUILD_DIR_LINT)/benchmark/app" BUILD_DIR_TESTS="$(BUILD_DIR_LINT)/benchmark/tests" BUILD_DIR_LINT="$(BUILD_DIR_LINT)/benchmark"

lint-benchmark-quick:
	@echo "Checking quick benchmark configuration"
	@$(MAKE) lint-configuration PRESET=benchmark-quick BUILD_DIR_APP="$(BUILD_DIR_LINT)/benchmark-quick/app" BUILD_DIR_TESTS="$(BUILD_DIR_LINT)/benchmark-quick/tests" BUILD_DIR_LINT="$(BUILD_DIR_LINT)/benchmark-quick"

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

# Keep the SDK installation when cleaning application and test outputs.
clean:
	@echo "Cleaning"
	@for dir in "$(BUILD_DIR_BASE)"/*; do \
	    [ "$$dir" = "$(BUILD_DIR_SDK)" ] || rm -rf -- "$$dir"; \
	done

clean-all: clean
	@echo "Cleaning SDK installations"
	@rm -rf -- "$(BUILD_DIR_SDK)"

diagnostics:
	@echo "Querying console diagnostics"
	@node tools/diagnostic/query.mjs "$(PS2_IP)" "$(DIAGNOSTIC)"

ps2link: sdk
	@echo "Building $(SDK_VARIANT) PS2Link"
	@sh tools/ps2link/build.sh --source-dir "$(PS2LINK_SOURCE)" --build-dir "$(BUILD_DIR_PS2LINK)/$(SDK_VARIANT)" --sdk-dir "$(BUILD_DIR_SDK)" --variant "$(SDK_VARIANT)"

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
	@. "$(SDK_ENVIRONMENT)" && node tools/milkdrop/benchmark.mjs "$(PS2_IP)" "$(BUILD_DIR_APP)"
