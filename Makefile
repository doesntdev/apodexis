CC ?= cc
AS ?= as
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
BUILD := .build
QBE_SOURCE := third_party/qbe
QBE := $(BUILD)/tools/qbe
FRONTEND := compiler/apx_frontend.c compiler/apx_qbe.c compiler/apx_metadata.c compiler/apx_hash.c compiler/apx_unicode.c
DRIVER_HELPERS := compiler/apx_modules.c compiler/apx_host.c
HEADERS := compiler/apx_frontend.h compiler/apx_qbe.h compiler/apx_hash.h compiler/apx_unicode.h runtime/apx_runtime.h

.PHONY: all toolchain test test-support test-sanitize smoke check clean dist
all: toolchain $(BUILD)/apx_runtime.o $(BUILD)/apx_start.o bin/apodexis bin/apx-mcp

$(QBE):
	$(MAKE) -C $(QBE_SOURCE) CC="$(CC)" -j6
	mkdir -p $(BUILD)/tools
	cp $(QBE_SOURCE)/qbe $(QBE)

toolchain: $(QBE)
	$(QBE) -h

$(BUILD)/apx_runtime.o: runtime/apx_runtime.c runtime/apx_runtime.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fno-pie -c runtime/apx_runtime.c -o $@

$(BUILD)/apx_start.o: runtime/apx_start.s
	mkdir -p $(BUILD)
	$(AS) runtime/apx_start.s -o $@

$(BUILD)/apx_json.o: runtime/apx_runtime.c runtime/apx_runtime.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DAPX_LIBRARY_ONLY -c runtime/apx_runtime.c -o $@

bin/apodexis: compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) compiler/apx_modules.h compiler/apx_host.h $(HEADERS) $(BUILD)/apx_json.o
	mkdir -p bin
	$(CC) $(CFLAGS) -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) $(BUILD)/apx_json.o -o $@

bin/apx-mcp: bin/apodexis
	cp bin/apodexis bin/apx-mcp

$(BUILD)/test-support: tests/test_support.c compiler/apx_hash.c compiler/apx_hash.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_support.c compiler/apx_hash.c $(BUILD)/apx_json.o -o $@

test-support: $(BUILD)/test-support
	$(BUILD)/test-support

check:
	$(CC) $(CFLAGS) -Icompiler -fsyntax-only compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS)
	$(CC) $(CFLAGS) -fsyntax-only runtime/apx_runtime.c
	$(CC) $(CFLAGS) -DAPX_LIBRARY_ONLY -fsyntax-only runtime/apx_runtime.c

$(BUILD)/test-conformance: tests/test_conformance.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_conformance.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-mcp: tests/test_mcp.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_mcp.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-regressions: tests/test_regressions.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_regressions.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-sandbox: tests/test_sandbox.c runtime/apx_runtime.c runtime/apx_runtime.h
	$(CC) $(CFLAGS) tests/test_sandbox.c runtime/apx_runtime.c -o $@

$(BUILD)/test-unicode: tests/test_unicode.c $(FRONTEND) $(HEADERS)
	$(CC) $(CFLAGS) -Icompiler tests/test_unicode.c $(FRONTEND) -o $@

$(BUILD)/test-frontend: tests/test_frontend.c $(FRONTEND) $(HEADERS)
	$(CC) $(CFLAGS) -Icompiler tests/test_frontend.c $(FRONTEND) -o $@

$(BUILD)/test-diagnostics: tests/test_diagnostics.c $(FRONTEND) $(HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -Icompiler tests/test_diagnostics.c $(FRONTEND) $(BUILD)/apx_json.o -o $@

$(BUILD)/test-diagnostic-transport: tests/test_diagnostic_transport.c $(FRONTEND) $(HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -Icompiler tests/test_diagnostic_transport.c $(FRONTEND) $(BUILD)/apx_json.o -o $@

$(BUILD)/test-authoring-syntax: tests/test_authoring_syntax.c $(FRONTEND) $(HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -Icompiler tests/test_authoring_syntax.c $(FRONTEND) $(BUILD)/apx_json.o -o $@

$(BUILD)/test-authoring-native: tests/test_authoring_native.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_authoring_native.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-pure-libraries: tests/test_pure_libraries.c $(FRONTEND) $(HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -Icompiler tests/test_pure_libraries.c $(FRONTEND) $(BUILD)/apx_json.o -o $@

$(BUILD)/test-application-foundation: tests/test_application_foundation.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_application_foundation.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-modules: tests/test_modules.c compiler/apx_modules.c compiler/apx_modules.h
	$(CC) $(CFLAGS) tests/test_modules.c compiler/apx_modules.c -o $@

$(BUILD)/test-host: tests/test_host.c compiler/apx_host.c compiler/apx_host.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_host.c compiler/apx_host.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-authoring-mcp: tests/test_authoring_mcp.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_authoring_mcp.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-reference-programs: tests/test_reference_programs.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_reference_programs.c $(BUILD)/apx_json.o -o $@

test-sanitize: all
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_fuzz.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-fuzz-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-fuzz-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_diagnostics.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-diagnostics-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-diagnostics-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_authoring_syntax.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-authoring-syntax-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-authoring-syntax-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_modules.c compiler/apx_modules.c -o $(BUILD)/test-modules-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-modules-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY tests/test_host.c compiler/apx_host.c runtime/apx_runtime.c -o $(BUILD)/test-host-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-host-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_pure_libraries.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-pure-libraries-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-pure-libraries-sanitized

test: all test-support $(BUILD)/test-conformance $(BUILD)/test-mcp $(BUILD)/test-regressions $(BUILD)/test-sandbox $(BUILD)/test-unicode $(BUILD)/test-frontend $(BUILD)/test-diagnostics $(BUILD)/test-diagnostic-transport $(BUILD)/test-authoring-syntax $(BUILD)/test-authoring-native $(BUILD)/test-reference-programs $(BUILD)/test-authoring-mcp $(BUILD)/test-pure-libraries $(BUILD)/test-modules $(BUILD)/test-host $(BUILD)/test-application-foundation
	$(BUILD)/test-pure-libraries
	$(BUILD)/test-modules
	$(BUILD)/test-host
	$(BUILD)/test-application-foundation
	$(BUILD)/test-authoring-mcp
	$(BUILD)/test-reference-programs
	$(BUILD)/test-authoring-syntax
	$(BUILD)/test-authoring-native
	$(BUILD)/test-diagnostic-transport
	$(BUILD)/test-diagnostics
	$(BUILD)/test-conformance
	$(BUILD)/test-mcp
	$(BUILD)/test-regressions
	$(BUILD)/test-sandbox
	$(BUILD)/test-unicode
	$(BUILD)/test-frontend --selftest
	bin/apodexis run examples/hello.apx --json
	bin/apodexis test examples/hello.apx --json
	bin/apodexis verify examples/double_values.apx --json
	bin/apodexis run examples/double_values.apx --input fixtures/double_values/values.input.json --json
	bin/apodexis run examples/rowdiff.apx --input fixtures/rowdiff/changed.input.json --json
	bin/apodexis test examples/rowdiff.apx --json
	bin/apodexis test examples/http_hello.apx --json
	bin/apodexis test examples/web_static.apx --json
	bin/apodexis test examples/compact_authoring.apx --json
	bin/apodexis run examples/compact_authoring.apx --input fixtures/compact_authoring/values.input.json --json

smoke: test

# Relocatable native distribution: no C/Python/Rust source needed by consumers.
dist: all
	rm -rf $(BUILD)/dist/apodexis-linux-amd64
	mkdir -p $(BUILD)/dist/apodexis-linux-amd64/bin $(BUILD)/dist/apodexis-linux-amd64/.build/tools $(BUILD)/dist/apodexis-linux-amd64/third_party/qbe
	cp bin/apodexis bin/apx-mcp $(BUILD)/dist/apodexis-linux-amd64/bin/
	cp $(QBE) $(BUILD)/dist/apodexis-linux-amd64/.build/tools/qbe
	cp $(BUILD)/apx_runtime.o $(BUILD)/apx_start.o $(BUILD)/dist/apodexis-linux-amd64/.build/
	cp -R resources examples fixtures $(BUILD)/dist/apodexis-linux-amd64/
	cp docs/binary-distribution.md $(BUILD)/dist/apodexis-linux-amd64/README.md
	cp LICENSE THIRD_PARTY_NOTICES.md $(BUILD)/dist/apodexis-linux-amd64/
	cp third_party/qbe/LICENSE $(BUILD)/dist/apodexis-linux-amd64/QBE-LICENSE
	cp third_party/qbe/LICENSE $(BUILD)/dist/apodexis-linux-amd64/third_party/qbe/LICENSE
	cp third_party/qbe-provenance.json $(BUILD)/dist/apodexis-linux-amd64/third_party/qbe-provenance.json
	tar -czf $(BUILD)/apodexis-linux-amd64.tar.gz -C $(BUILD)/dist apodexis-linux-amd64

clean:
	rm -rf $(BUILD)
	rm -f bin/apodexis bin/apx-mcp
	$(MAKE) -C $(QBE_SOURCE) clean
