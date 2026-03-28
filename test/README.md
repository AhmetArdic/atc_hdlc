# Running Tests

Build the project first:

```sh
cmake -B build
cmake --build build
```

## Run all tests

```sh
ctest --test-dir build
```

## Filter by label

```sh
ctest --test-dir build -L unit
ctest --test-dir build -L integration
```

## Filter by name

```sh
# All tests in a suite
ctest --test-dir build -R "connection_management\."

# A single test
ctest --test-dir build -R "virtual_com\.go_back_n_w4"
```

## List without running

```sh
ctest --test-dir build -N
ctest --test-dir build -N -L unit
```

## Labels

| Label         | Description                        |
|---------------|------------------------------------|
| `unit`        | Fast, no I/O, no threads           |
| `integration` | Virtual pipe tests (no hardware)   |

## Physical target test (requires MCU)

`integration/physical_target.c` is a PC-side test runner that communicates with a real MCU
over a serial port. It is **not** registered as a ctest entry because it requires hardware.

**Prerequisites:** the MCU must be running the code from `mcu_test_template/` — copy those
files into your MCU project, implement the three platform functions in `hdlc_platform.h`,
then flash the MCU. See `mcu_test_template/README.md` for full integration instructions.

Once the MCU is connected and running, execute the PC-side runner directly:

```sh
# All window sizes (1–7), auto-detect defaults
build/test/integration/physical_target

# Specific serial port and baud rate
build/test/integration/physical_target --port /dev/ttyUSB0 --baud 921600

# Single window size
build/test/integration/physical_target --port /dev/ttyUSB0 --baud 921600 w4
```

## mcu_test_template/

`mcu_test_template/` is a **template** — not compiled by this build system. Copy its contents
into your MCU project and implement the platform functions to enable MCU integration testing.
