CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS := -Iinclude
BUILD := build
COMMON := $(BUILD)/lwp_protocol.o $(BUILD)/lwp_context.o

.PHONY: all clean test

all: $(BUILD)/lwp_sender $(BUILD)/lwp_receiver $(BUILD)/test_protocol

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/lwp_protocol.o: src/lwp_protocol.c include/lwp_protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/lwp_context.o: src/lwp_context.c include/lwp_context.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/lwp_sender.o: src/lwp_sender.c include/lwp_protocol.h include/lwp_context.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/lwp_receiver.o: src/lwp_receiver.c include/lwp_protocol.h include/lwp_context.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_protocol.o: tests/test_protocol.c include/lwp_protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/lwp_sender: $(BUILD)/lwp_sender.o $(COMMON)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/lwp_receiver: $(BUILD)/lwp_receiver.o $(COMMON)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_protocol: $(BUILD)/test_protocol.o $(BUILD)/lwp_protocol.o
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILD)/test_protocol
	./$(BUILD)/test_protocol

clean:
	rm -rf $(BUILD)

BENCHMARK_BINS := $(BUILD)/tcp_sender $(BUILD)/tcp_receiver \
                  $(BUILD)/udp_sender $(BUILD)/udp_receiver \
                  $(BUILD)/packet_meter

all: $(BENCHMARK_BINS)

$(BUILD)/tcp_sender: benchmarks/tcp_sender.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/tcp_receiver: benchmarks/tcp_receiver.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/udp_sender: benchmarks/udp_sender.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/udp_receiver: benchmarks/udp_receiver.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/packet_meter: benchmarks/packet_meter.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@
