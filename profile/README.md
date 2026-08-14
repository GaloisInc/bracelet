# Profiling

The Justfile in this directory provides some commands for building the llvm-test-suite with various compiler configurations for comparison and profiling.

[Just](https://github.com/casey/just) can be installed with `cargo install just`

To setup the environment, edit the .env to point to the install prefix for the version of BRACELET that you would like to profile.

If you want to generate callgrind data, ensure this prefix was configured with profile=true.

Additionally, uv should be synced/setup prior to invocation. 

## Commands
- `just run-suite-upstream` builds with the baseline upstream compiler to collect baseline timing metrics
- `just run-suite-bracelet` builds with the provided prefix but with debug data enabled
- `just run-suite-bracelet-nodebug` builds with the provided prefix but debug data disabled
- `just search-for-prof <build dir>` for the given build dir collect callgrind logs that contain at least one call to a bracelet function and merge them into a single callgrind that can be loaded by qcallgrind.
- `uv run python parser.py <results.json1> <results.json2> --name1 <col name 1> --name2 <col name 2> -o <output csv>` collect compile time metrics from both llvm-test-suite runs and calculate percentage change from the second result to the first result.  
- alternatively `just aggregate-timing <target1> <target2> <name1> <name2>
