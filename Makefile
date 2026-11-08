CC ?= cc
AS ?= as
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
BUILD := .build
QBE_SOURCE := third_party/qbe
QBE := $(BUILD)/tools/qbe
FRONTEND := compiler/apx_frontend.c compiler/apx_qbe.c compiler/apx_metadata.c compiler/apx_hash.c compiler/apx_unicode.c
DRIVER_HELPERS := compiler/apx_modules.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c compiler/apx_serve.c
HEADERS := compiler/apx_serve.h compiler/apx_pqc.h compiler/apx_frontend.h compiler/apx_qbe.h compiler/apx_hash.h compiler/apx_unicode.h runtime/apx_runtime.h

.PHONY: all toolchain test test-support test-memory test-sanitize smoke check clean dist
all: $(BUILD)/tools/apx-http toolchain $(BUILD)/apx_runtime.o $(BUILD)/apx_start.o bin/apodexis bin/apx-mcp

$(QBE):
	$(MAKE) -C $(QBE_SOURCE) CC="$(CC)" -j6
	mkdir -p $(BUILD)/tools
	cp $(QBE_SOURCE)/qbe $(QBE)

$(BUILD)/tools/apx-http: compiler/apx_http.c compiler/apx_http.h compiler/apx_http_main.c $(BUILD)/apx_json.o
	mkdir -p $(BUILD)/tools
	$(CC) $(CFLAGS) compiler/apx_http_main.c compiler/apx_http.c $(BUILD)/apx_json.o -o $@

.PHONY: http-tls
http-tls: $(BUILD)/tools/apx-http-tls
$(BUILD)/tools/apx-http-tls: compiler/apx_http.c compiler/apx_http.h compiler/apx_http_main.c $(BUILD)/apx_json.o
	mkdir -p $(BUILD)/tools
	$(CC) $(CFLAGS) -DAPX_HTTP_OPENSSL $$(pkg-config --cflags openssl) compiler/apx_http_main.c compiler/apx_http.c $(BUILD)/apx_json.o $$(pkg-config --libs openssl) -o $@

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

bin/apodexis: compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) compiler/apx_modules.h compiler/apx_host.h compiler/apx_http.h $(HEADERS) $(BUILD)/apx_json.o
	mkdir -p bin
	$(CC) $(CFLAGS) -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) $(BUILD)/apx_json.o -o $@

bin/apx-mcp: bin/apodexis
	cp bin/apodexis bin/apx-mcp

$(BUILD)/test-support: tests/test_support.c compiler/apx_hash.c compiler/apx_hash.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_support.c compiler/apx_hash.c $(BUILD)/apx_json.o -o $@

test-support: $(BUILD)/test-support
	$(BUILD)/test-support

$(BUILD)/test-runtime-memory: tests/test_runtime_memory.c runtime/apx_runtime.c runtime/apx_runtime.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) tests/test_runtime_memory.c -o $@

test-memory: $(BUILD)/test-runtime-memory
	$(BUILD)/test-runtime-memory

check:
	$(CC) $(CFLAGS) -Icompiler -fsyntax-only compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS)
	$(CC) $(CFLAGS) -fsyntax-only runtime/apx_runtime.c
	$(CC) $(CFLAGS) -DAPX_LIBRARY_ONLY -fsyntax-only runtime/apx_runtime.c

$(BUILD)/test-conformance: tests/test_conformance.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_conformance.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-compiled-artifacts: tests/test_compiled_artifacts.c compiler/apx_hash.c compiler/apx_hash.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_compiled_artifacts.c compiler/apx_hash.c $(BUILD)/apx_json.o -o $@

# Instrument the actual driver/store and JSON arena, not just a test client.
.PHONY: test-compiled-artifacts-sanitize
test-compiled-artifacts-sanitize: all $(BUILD)/test-compiled-artifacts
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) runtime/apx_runtime.c -o $(BUILD)/apx-mcp-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 APX_TEST_MCP_BINARY=$(BUILD)/apx-mcp-sanitized $(BUILD)/test-compiled-artifacts

$(BUILD)/test-mcp: tests/test_mcp.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_mcp.c $(BUILD)/apx_json.o -o $@

.PHONY: test-serving test-http-credentials test-serving-sanitize test-http-credentials-sanitize
$(BUILD)/test-serving: tests/test_serving.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_serving.c $(BUILD)/apx_json.o -o $@
test-serving: all $(BUILD)/test-serving
	$(BUILD)/test-serving
$(BUILD)/test-http-credentials: tests/test_http_credentials.c compiler/apx_host.c compiler/apx_host.h compiler/apx_http.c compiler/apx_http.h compiler/apx_pqc.c compiler/apx_pqc.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_http_credentials.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c $(BUILD)/apx_json.o -o $@
test-http-credentials: all $(BUILD)/test-http-credentials
	$(BUILD)/test-http-credentials
# Exercise the actual serving parent, driver callbacks and broker with ASan/UBSan.
# Keep the single ordinary toolchain cache; no copied toolchain/build trees.
test-serving-sanitize: all $(BUILD)/test-serving
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) runtime/apx_runtime.c -o $(BUILD)/apx-serve-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 APX_TEST_SERVE_BINARY=$(abspath $(BUILD)/apx-serve-sanitized) $(BUILD)/test-serving
test-http-credentials-sanitize: all
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY tests/test_http_credentials.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c runtime/apx_runtime.c -o $(BUILD)/test-http-credentials-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-http-credentials-sanitized

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

$(BUILD)/test-native-scalars: tests/test_native_scalars.c $(FRONTEND) $(HEADERS) runtime/apx_runtime.c
	$(CC) $(CFLAGS) -Icompiler tests/test_native_scalars.c $(FRONTEND) -o $@

$(BUILD)/test-native-collections: tests/test_native_collections.c $(FRONTEND) $(HEADERS) runtime/apx_runtime.c
	$(CC) $(CFLAGS) -Icompiler tests/test_native_collections.c $(FRONTEND) -o $@

.PHONY: test-native-calls
.PHONY: test-native-json
$(BUILD)/test-native-json: tests/test_native_json.c $(FRONTEND) $(HEADERS) runtime/apx_runtime.c
	$(CC) $(CFLAGS) -Icompiler tests/test_native_json.c $(FRONTEND) -o $@

test-native-json: all $(BUILD)/test-native-json
	$(BUILD)/test-native-json

$(BUILD)/test-native-calls: tests/test_native_calls.c $(FRONTEND) $(HEADERS) runtime/apx_runtime.c
	$(CC) $(CFLAGS) -Icompiler tests/test_native_calls.c $(FRONTEND) -o $@

test-native-calls: all $(BUILD)/test-native-calls
	$(BUILD)/test-native-calls

.PHONY: test-native-shapes
$(BUILD)/test-native-shapes: tests/test_native_shapes.c $(FRONTEND) $(HEADERS) runtime/apx_runtime.c
	$(CC) $(CFLAGS) -Icompiler tests/test_native_shapes.c $(FRONTEND) -o $@

test-native-shapes: all $(BUILD)/test-native-shapes
	$(BUILD)/test-native-shapes

.PHONY: test-native-collections
test-native-collections: all $(BUILD)/test-native-collections
	$(BUILD)/test-native-collections

.PHONY: test-native-scalars
test-native-scalars: all $(BUILD)/test-native-scalars
	$(BUILD)/test-native-scalars

$(BUILD)/test-pure-libraries: tests/test_pure_libraries.c $(FRONTEND) $(HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -Icompiler tests/test_pure_libraries.c $(FRONTEND) $(BUILD)/apx_json.o -o $@

$(BUILD)/test-application-foundation: tests/test_application_foundation.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_application_foundation.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-modules: tests/test_modules.c compiler/apx_modules.c compiler/apx_modules.h
	$(CC) $(CFLAGS) tests/test_modules.c compiler/apx_modules.c -o $@

$(BUILD)/test-host: tests/test_host.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c compiler/apx_host.h compiler/apx_http.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_host.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-authoring-mcp: tests/test_authoring_mcp.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_authoring_mcp.c $(BUILD)/apx_json.o -o $@

$(BUILD)/test-reference-programs: tests/test_reference_programs.c $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_reference_programs.c $(BUILD)/apx_json.o -o $@

test-sanitize: all
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icompiler tests/test_native_json.c $(FRONTEND) -o $(BUILD)/test-native-json-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-native-json-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icompiler tests/test_native_calls.c $(FRONTEND) -o $(BUILD)/test-native-calls-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-native-calls-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icompiler tests/test_native_shapes.c $(FRONTEND) -o $(BUILD)/test-native-shapes-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-native-shapes-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icompiler tests/test_native_collections.c $(FRONTEND) -o $(BUILD)/test-native-collections-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-native-collections-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Icompiler tests/test_native_scalars.c $(FRONTEND) -o $(BUILD)/test-native-scalars-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-native-scalars-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_runtime_memory.c -o $(BUILD)/test-runtime-memory-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-runtime-memory-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY tests/test_support.c compiler/apx_hash.c runtime/apx_runtime.c -o $(BUILD)/test-support-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-support-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_fuzz.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-fuzz-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-fuzz-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_diagnostics.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-diagnostics-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-diagnostics-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_authoring_syntax.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-authoring-syntax-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-authoring-syntax-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_modules.c compiler/apx_modules.c -o $(BUILD)/test-modules-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-modules-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY tests/test_host.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c runtime/apx_runtime.c -o $(BUILD)/test-host-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-host-sanitized
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -DAPX_LIBRARY_ONLY -Icompiler tests/test_pure_libraries.c $(FRONTEND) runtime/apx_runtime.c -o $(BUILD)/test-pure-libraries-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/test-pure-libraries-sanitized

test: test-serving test-http-credentials test-http all test-support test-memory $(BUILD)/test-compiled-artifacts $(BUILD)/test-native-json $(BUILD)/test-native-calls $(BUILD)/test-native-shapes $(BUILD)/test-native-collections $(BUILD)/test-native-scalars $(BUILD)/test-conformance $(BUILD)/test-mcp $(BUILD)/test-regressions $(BUILD)/test-sandbox $(BUILD)/test-unicode $(BUILD)/test-frontend $(BUILD)/test-diagnostics $(BUILD)/test-diagnostic-transport $(BUILD)/test-authoring-syntax $(BUILD)/test-authoring-native $(BUILD)/test-reference-programs $(BUILD)/test-authoring-mcp $(BUILD)/test-pure-libraries $(BUILD)/test-modules $(BUILD)/test-host $(BUILD)/test-application-foundation
	$(BUILD)/test-native-json
	$(BUILD)/test-native-calls
	$(BUILD)/test-native-shapes
	$(BUILD)/test-pure-libraries
	$(BUILD)/test-modules
	$(BUILD)/test-host
	$(BUILD)/test-application-foundation
	$(BUILD)/test-native-collections
	$(BUILD)/test-native-scalars
	$(BUILD)/test-authoring-mcp
	$(BUILD)/test-compiled-artifacts
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
	bin/apodexis test examples/http_client/report.apx --json
	bin/apodexis test examples/web_static.apx --json
	bin/apodexis test examples/compact_authoring.apx --json
	bin/apodexis run examples/compact_authoring.apx --input fixtures/compact_authoring/values.input.json --json

$(BUILD)/test-http-client: tests/test_http_client.c compiler/apx_host.c compiler/apx_host.h compiler/apx_http.c compiler/apx_http.h compiler/apx_pqc.c compiler/apx_pqc.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) tests/test_http_client.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c $(BUILD)/apx_json.o -o $@
$(BUILD)/test-http-client-tls: tests/test_http_client.c compiler/apx_host.c compiler/apx_host.h compiler/apx_http.c compiler/apx_http.h compiler/apx_pqc.c compiler/apx_pqc.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -DAPX_HTTP_OPENSSL $$(pkg-config --cflags openssl) tests/test_http_client.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c $(BUILD)/apx_json.o $$(pkg-config --libs openssl) -o $@
$(BUILD)/test-http-client-pq: tests/test_http_client.c compiler/apx_host.c compiler/apx_host.h compiler/apx_http.c compiler/apx_http.h compiler/apx_pqc.c compiler/apx_pqc.h $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -DAPX_HTTP_OPENSSL -DAPX_HTTP_PQ $$(pkg-config --cflags openssl) tests/test_http_client.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c $(BUILD)/apx_json.o $$(pkg-config --libs openssl) -o $@
.PHONY: test-http test-http-tls test-http-pq
# Explicit qualification: unavailable hybrid TLS is a failure, never a skip.
test-http-pq: all http-tls $(BUILD)/tools/apx-http-dns-stall $(BUILD)/tools/apx-http-pq-unavailable $(BUILD)/test-http-client-pq
	$(BUILD)/test-http-client-pq
$(BUILD)/http-pq-unavailable-wrapper.o: tests/test_http_client.c
	$(CC) $(CFLAGS) -DAPX_HTTP_PQ_UNAVAILABLE_WRAPPER $$(pkg-config --cflags openssl) -c $< -o $@
$(BUILD)/tools/apx-http-pq-unavailable: compiler/apx_http_main.c compiler/apx_http.c compiler/apx_http.h $(BUILD)/http-pq-unavailable-wrapper.o $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -DAPX_HTTP_OPENSSL $$(pkg-config --cflags openssl) compiler/apx_http_main.c compiler/apx_http.c $(BUILD)/apx_json.o $(BUILD)/http-pq-unavailable-wrapper.o -Wl,--wrap=SSL_CTX_ctrl $$(pkg-config --libs openssl) -o $@
$(BUILD)/http-resolver-wrapper.o: tests/test_http_client.c
	$(CC) $(CFLAGS) -DAPX_HTTP_RESOLVER_WRAPPER -c $< -o $@
$(BUILD)/tools/apx-http-dns-stall: compiler/apx_http_main.c compiler/apx_http.c $(BUILD)/http-resolver-wrapper.o $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) compiler/apx_http_main.c compiler/apx_http.c $(BUILD)/apx_json.o $(BUILD)/http-resolver-wrapper.o -Wl,--wrap=getaddrinfo -o $@
test-http: all $(BUILD)/tools/apx-http-dns-stall $(BUILD)/test-http-client
	$(BUILD)/test-http-client
test-http-tls: all http-tls $(BUILD)/tools/apx-http-dns-stall $(BUILD)/test-http-client-tls
	$(BUILD)/test-http-client-tls

# Dedicated minimal layout; source, QBE and startup objects are shared, never copied.
HTTP_SANITIZE_TLS ?= 0
HTTP_SANITIZE_PQ ?= 0
SAN_FLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
.PHONY: test-http-sanitize
test-http-sanitize: all $(BUILD)/http-resolver-wrapper.o
	mkdir -p $(BUILD)/http-sanitize/bin $(BUILD)/http-sanitize/.build/tools
	ln -sfn $(abspath $(QBE)) $(BUILD)/http-sanitize/.build/tools/qbe
	ln -sfn $(abspath $(BUILD)/apx_start.o) $(BUILD)/http-sanitize/.build/apx_start.o
	ln -sfn $(abspath examples) $(BUILD)/http-sanitize/examples
	ln -sfn $(abspath resources) $(BUILD)/http-sanitize/resources
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) runtime/apx_runtime.c -o $(BUILD)/http-sanitize/bin/apodexis
	$(CC) $(CFLAGS) -fno-pie -fsanitize=undefined -fsanitize-undefined-trap-on-error -c runtime/apx_runtime.c -o $(BUILD)/http-sanitize/.build/apx_runtime.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY compiler/apx_http_main.c compiler/apx_http.c runtime/apx_runtime.c -o $(BUILD)/http-sanitize/.build/tools/apx-http
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY compiler/apx_http_main.c compiler/apx_http.c runtime/apx_runtime.c $(BUILD)/http-resolver-wrapper.o -Wl,--wrap=getaddrinfo -o $(BUILD)/http-sanitize/.build/tools/apx-http-dns-stall
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY tests/test_http_client.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c runtime/apx_runtime.c -o $(BUILD)/test-http-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 APX_TEST_TOOLCHAIN_ROOT=$(abspath $(BUILD)/http-sanitize) $(BUILD)/test-http-sanitized
ifeq ($(HTTP_SANITIZE_TLS),1)
ifeq ($(HTTP_SANITIZE_PQ),1)
	$(CC) $(CFLAGS) -DAPX_HTTP_PQ_UNAVAILABLE_WRAPPER $$(pkg-config --cflags openssl) -c tests/test_http_client.c -o $(BUILD)/http-pq-unavailable-wrapper.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -DAPX_HTTP_OPENSSL $$(pkg-config --cflags openssl) compiler/apx_http_main.c compiler/apx_http.c runtime/apx_runtime.c $(BUILD)/http-pq-unavailable-wrapper.o -Wl,--wrap=SSL_CTX_ctrl $$(pkg-config --libs openssl) -o $(BUILD)/http-sanitize/.build/tools/apx-http-pq-unavailable
endif
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -DAPX_HTTP_OPENSSL $$(pkg-config --cflags openssl) compiler/apx_http_main.c compiler/apx_http.c runtime/apx_runtime.c $$(pkg-config --libs openssl) -o $(BUILD)/http-sanitize/.build/tools/apx-http-tls
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -DAPX_HTTP_OPENSSL $(if $(filter 1,$(HTTP_SANITIZE_PQ)),-DAPX_HTTP_PQ,) $$(pkg-config --cflags openssl) tests/test_http_client.c compiler/apx_host.c compiler/apx_http.c compiler/apx_pqc.c runtime/apx_runtime.c $$(pkg-config --libs openssl) -o $(BUILD)/test-http-tls-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 APX_TEST_TOOLCHAIN_ROOT=$(abspath $(BUILD)/http-sanitize) $(BUILD)/test-http-tls-sanitized
endif

# Explicit optional integration only; never a dependency of all/check/test.
.PHONY: test-repo-brief
test-repo-brief: all
	python3 -B tests/test_repo_brief.py

smoke: test

# Relocatable native distribution: no C/Python/Rust source needed by consumers.
DIST_PROFILE ?= plain
DIST_NAME := apodexis-linux-amd64$(if $(filter tls,$(DIST_PROFILE)),-http,$(if $(filter pqc,$(DIST_PROFILE)),-pqc,))
dist: all
	rm -rf $(BUILD)/dist/$(DIST_NAME)
	mkdir -p $(BUILD)/dist/$(DIST_NAME)/bin $(BUILD)/dist/$(DIST_NAME)/.build/tools $(BUILD)/dist/$(DIST_NAME)/third_party/qbe
	cp bin/apodexis bin/apx-mcp $(BUILD)/dist/$(DIST_NAME)/bin/
	cp $(QBE) $(BUILD)/dist/$(DIST_NAME)/.build/tools/qbe
	cp $(BUILD)/tools/apx-http $(BUILD)/dist/$(DIST_NAME)/.build/tools/apx-http
ifneq ($(filter tls pqc,$(DIST_PROFILE)),)
	cp $(BUILD)/tools/apx-http-tls $(BUILD)/dist/$(DIST_NAME)/.build/tools/apx-http-tls
endif
	printf '%s\n' 'profile=$(DIST_PROFILE)' 'http=libc' 'https=$(if $(filter tls pqc,$(DIST_PROFILE)),OpenSSL-system,unavailable)' > $(BUILD)/dist/$(DIST_NAME)/HTTP-PROFILE
ifeq ($(DIST_PROFILE),pqc)
	cp $(BUILD)/tools/apx-pqc $(BUILD)/dist/$(DIST_NAME)/.build/tools/apx-pqc
	cp third_party/OPENSSL-LICENSE $(BUILD)/dist/$(DIST_NAME)/OPENSSL-LICENSE
	cp third_party/OPENSSL-LICENSE third_party/openssl-pqc-provenance.json $(BUILD)/dist/$(DIST_NAME)/third_party/
	printf '%s\n' 'profile=cms-authenveloped-mlkem768-aes256gcm-v1' 'crypto=static-libcrypto-default-provider' 'key-format=SPKI-PKCS8-PEM' 'identity=operator-pinned-SPKI' > $(BUILD)/dist/$(DIST_NAME)/PQC-PROFILE
endif
	cp $(BUILD)/apx_runtime.o $(BUILD)/apx_start.o $(BUILD)/dist/$(DIST_NAME)/.build/
	cp -R resources examples fixtures $(BUILD)/dist/$(DIST_NAME)/
	cp docs/binary-distribution.md $(BUILD)/dist/$(DIST_NAME)/README.md
	mkdir -p $(BUILD)/dist/$(DIST_NAME)/docs
	cp docs/http-client.md docs/post-quantum.md docs/compiled-artifacts.md docs/config-validator.md docs/tool-serving.md docs/repo-brief.md $(BUILD)/dist/$(DIST_NAME)/docs/
	cp LICENSE THIRD_PARTY_NOTICES.md $(BUILD)/dist/$(DIST_NAME)/
	cp third_party/qbe/LICENSE $(BUILD)/dist/$(DIST_NAME)/QBE-LICENSE
	cp third_party/qbe/LICENSE $(BUILD)/dist/$(DIST_NAME)/third_party/qbe/LICENSE
	cp third_party/qbe-provenance.json $(BUILD)/dist/$(DIST_NAME)/third_party/qbe-provenance.json
	tar -czf $(BUILD)/$(DIST_NAME).tar.gz -C $(BUILD)/dist $(DIST_NAME)

.PHONY: dist-http
dist-http: http-tls
	$(MAKE) dist DIST_PROFILE=tls

# Explicit optional backend: never downloaded, installed or selected by source.
PQC_OPENSSL_PREFIX ?=
PQC_LIBDIR = $(if $(wildcard $(PQC_OPENSSL_PREFIX)/lib64/libcrypto.a),$(PQC_OPENSSL_PREFIX)/lib64,$(PQC_OPENSSL_PREFIX)/lib)
PQC_CFLAGS = $(if $(PQC_OPENSSL_PREFIX),-I$(PQC_OPENSSL_PREFIX)/include,)
PQC_CRYPTO = $(if $(PQC_OPENSSL_PREFIX),$(PQC_LIBDIR)/libcrypto.a,$(shell $(CC) -print-file-name=libcrypto.a)) -ldl -pthread
PQC_SOURCES := compiler/apx_pqc_main.c compiler/apx_pqc.c compiler/apx_pqc_profile.c compiler/apx_http.c
PQC_HEADERS := compiler/apx_pqc.h compiler/apx_pqc_profile.h compiler/apx_http.h
.PHONY: pqc test-pqc test-pqc-sanitize dist-pqc
pqc: $(BUILD)/tools/apx-pqc
$(BUILD)/tools/apx-pqc: $(PQC_SOURCES) $(PQC_HEADERS) $(BUILD)/apx_json.o $(if $(PQC_OPENSSL_PREFIX),$(PQC_LIBDIR)/libcrypto.a,)
	mkdir -p $(BUILD)/tools
	$(CC) $(CFLAGS) -DAPX_PQC_OPENSSL $(PQC_CFLAGS) $(PQC_SOURCES) $(BUILD)/apx_json.o $(PQC_CRYPTO) -o $@

$(BUILD)/test-pqc: tests/test_pqc.c compiler/apx_host.c compiler/apx_host.h compiler/apx_pqc.c compiler/apx_pqc_profile.c compiler/apx_http.c $(PQC_HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) $(PQC_CFLAGS) tests/test_pqc.c compiler/apx_host.c compiler/apx_pqc.c compiler/apx_pqc_profile.c compiler/apx_http.c $(BUILD)/apx_json.o $(PQC_CRYPTO) -o $@
$(BUILD)/tools/apx-pqc-fixture: tests/test_pqc.c compiler/apx_pqc.c compiler/apx_http.c $(PQC_HEADERS) $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -DAPX_PQC_FIXTURE tests/test_pqc.c compiler/apx_pqc.c compiler/apx_http.c $(BUILD)/apx_json.o -o $@
$(BUILD)/pqc-keygen-fail.o: tests/test_pqc.c
	$(CC) $(CFLAGS) -DAPX_PQC_KEYGEN_FAIL -c $< -o $@
$(BUILD)/tools/apx-pqc-keygen-fail: $(PQC_SOURCES) $(PQC_HEADERS) $(BUILD)/pqc-keygen-fail.o $(BUILD)/apx_json.o
	$(CC) $(CFLAGS) -DAPX_PQC_OPENSSL $(PQC_CFLAGS) $(PQC_SOURCES) $(BUILD)/apx_json.o $(BUILD)/pqc-keygen-fail.o -Wl,--wrap=fsync $(PQC_CRYPTO) -o $@
test-pqc: all pqc $(BUILD)/test-pqc $(BUILD)/tools/apx-pqc-fixture $(BUILD)/tools/apx-pqc-keygen-fail
	APX_TEST_OPENSSL=$(if $(PQC_OPENSSL_PREFIX),$(PQC_OPENSSL_PREFIX)/bin/openssl,/usr/bin/openssl) $(BUILD)/test-pqc

test-pqc-sanitize: all pqc $(BUILD)/pqc-keygen-fail.o
	mkdir -p $(BUILD)/pqc-sanitize/bin $(BUILD)/pqc-sanitize/.build/tools
	ln -sfn $(abspath $(QBE)) $(BUILD)/pqc-sanitize/.build/tools/qbe
	ln -sfn $(abspath $(BUILD)/apx_start.o) $(BUILD)/pqc-sanitize/.build/apx_start.o
	ln -sfn $(abspath resources) $(BUILD)/pqc-sanitize/resources
	ln -sfn $(abspath examples) $(BUILD)/pqc-sanitize/examples
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -Icompiler compiler/apx_driver.c $(FRONTEND) $(DRIVER_HELPERS) runtime/apx_runtime.c -o $(BUILD)/pqc-sanitize/bin/apodexis
	$(CC) $(CFLAGS) -fno-pie -fsanitize=undefined -fsanitize-undefined-trap-on-error -c runtime/apx_runtime.c -o $(BUILD)/pqc-sanitize/.build/apx_runtime.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_PQC_OPENSSL -DAPX_PQC_SANITIZE -DAPX_LIBRARY_ONLY $(PQC_CFLAGS) $(PQC_SOURCES) runtime/apx_runtime.c $(PQC_CRYPTO) -o $(BUILD)/pqc-sanitize/.build/tools/apx-pqc
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY -DAPX_PQC_FIXTURE tests/test_pqc.c compiler/apx_pqc.c compiler/apx_http.c runtime/apx_runtime.c -o $(BUILD)/pqc-sanitize/.build/tools/apx-pqc-fixture
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_PQC_OPENSSL -DAPX_PQC_SANITIZE -DAPX_LIBRARY_ONLY $(PQC_CFLAGS) $(PQC_SOURCES) runtime/apx_runtime.c $(BUILD)/pqc-keygen-fail.o -Wl,--wrap=fsync $(PQC_CRYPTO) -o $(BUILD)/pqc-sanitize/.build/tools/apx-pqc-keygen-fail
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DAPX_LIBRARY_ONLY $(PQC_CFLAGS) tests/test_pqc.c compiler/apx_host.c compiler/apx_pqc.c compiler/apx_pqc_profile.c compiler/apx_http.c runtime/apx_runtime.c $(PQC_CRYPTO) -o $(BUILD)/test-pqc-sanitized
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 APX_TEST_OPENSSL=$(if $(PQC_OPENSSL_PREFIX),$(PQC_OPENSSL_PREFIX)/bin/openssl,/usr/bin/openssl) APX_TEST_TOOLCHAIN_ROOT=$(abspath $(BUILD)/pqc-sanitize) $(BUILD)/test-pqc-sanitized

ifeq ($(DIST_PROFILE),pqc)
dist: pqc http-tls third_party/OPENSSL-LICENSE third_party/openssl-pqc-provenance.json
endif
# HTTPS retains the separately qualified system TLS libraries and roots;
# the crypto helper alone embeds its verified static backend.
dist-pqc: pqc http-tls third_party/OPENSSL-LICENSE third_party/openssl-pqc-provenance.json
	$(MAKE) dist DIST_PROFILE=pqc

clean:
	rm -rf $(BUILD)
	rm -f bin/apodexis bin/apx-mcp
	$(MAKE) -C $(QBE_SOURCE) clean
