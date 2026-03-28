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
| `integration` | Virtual pipe or serial port tests  |
