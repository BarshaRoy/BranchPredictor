# Branch Predictor Simulator

This repository contains a C implementation of a branch predictor simulator. The simulator supports bimodal, gshare, and hybrid branch predictors and prints results in the assignment's required format.

## Project Structure

```text
.
├── sim.c                  # Branch predictor simulator source code
├── Makefile               # Build and clean targets
├── docs/
│   └── MP2_Instructions.pdf
├── traces/                # Input branch traces
│   ├── gcc_trace.txt
│   ├── jpeg_trace.txt
│   └── perl_trace.txt
├── validation/            # Provided expected outputs
│   ├── val_bimodal_*.txt
│   ├── val_gshare_*.txt
│   ├── val_hybrid_1.txt
│   └── val_smith_*.txt
└── outputs/               # Locally generated comparison outputs, ignored by Git
```

The trace files are stored in `traces/` to keep the repository root clean. The simulator accepts paths such as `traces/gcc_trace.txt` while still printing `gcc_trace.txt` in the `COMMAND` section, so the output format remains compatible with the validation files.

## Build

```bash
make
```

This creates the executable:

```text
./sim
```

To remove generated build files:

```bash
make clean
```

## Usage

Run the simulator from the repository root.

### Bimodal

```bash
./sim bimodal <M2> <tracefile>
```

Example:

```bash
./sim bimodal 6 traces/gcc_trace.txt
```

### Gshare

```bash
./sim gshare <M1> <N> <tracefile>
```

Example:

```bash
./sim gshare 9 3 traces/gcc_trace.txt
```

### Hybrid

```bash
./sim hybrid <K> <M1> <N> <M2> <tracefile>
```

Example:

```bash
./sim hybrid 8 14 10 5 traces/gcc_trace.txt
```

## Implementation Notes

- Bimodal and gshare predictor tables use 3-bit saturating counters.
- Counter values `0` through `3` predict not taken.
- Counter values `4` through `7` predict taken.
- Predictor counters are initialized to `4`.
- The hybrid predictor uses a 2-bit chooser table initialized to `1`.
- The global branch history register is updated after each branch using the actual outcome.

## Validation

Example validation workflow:

```bash
make
./sim bimodal 6 traces/gcc_trace.txt > outputs/test_bimodal.txt
diff -u validation/val_bimodal_1.txt outputs/test_bimodal.txt
```

No output from `diff` means the generated result matches the expected validation file.

Generated files in `outputs/` are ignored by Git so validation runs do not clutter commits.

## Repository Notes

The trace files are included because they are needed to reproduce the simulator outputs. If a hosting platform or course submission system requires a smaller upload, remove `traces/*.txt` from the repository and document where the traces should be downloaded or placed.
